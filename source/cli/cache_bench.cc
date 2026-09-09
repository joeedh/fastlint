#include "cli/files.h"
#include "fastlint/ast/file.h"
#include "fastlint/ast/kind_info.h"
#include "fastlint/ast/lower.h"
#include "fastlint/ast/node.h"
#include "fastlint/cache/closure.h"
#include "fastlint/cache/store.h"
#include "fastlint/syntax/diagnostics.h"
#include "fastlint/syntax/parser.h"
#include "fastlint/syntax/tree.h"
#include "fastlint/tsgo/client.h"
#include "fastlint/tsgo/queries.h"
#include "fastlint/tsgo/source_file.h"
#include "fastlint/types/type_facts.h"
#include "fastlint/types/type_graph.h"
#include "util/vector.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
// After windows.h by requirement.
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

// `lintrix cache-bench`: two passes over a project through the type cache. The cold pass
// starts from an empty database and fetches every expression's type from tsgo; the warm
// pass reopens the same database and replays. Both report timings per phase, the peak
// working set and the database size (MASTER 5.5).

namespace fastlint::cli {

using litestl::util::span;
using litestl::util::Vector;
using string = litestl::util::string;

namespace {

struct Options {
  std::string tsconfig;
  std::string cache;
  size_t limit = 0;
  bool keep = false;
  bool json = false;
  /** Report which node kinds fail to map onto tsgo nodes, with samples from the worst
   * file. */
  bool unmapped = false;
};

constexpr int kKindCount = int(ast::NodeKind::Error) + 1;

/** Per-kind tally of expression nodes without a tsgo counterpart. */
struct UnmappedReport {
  int byKind[kKindCount] = {};
  size_t total = 0;
  std::string worstFile;
  size_t worstCount = 0;
  Vector<std::string> worstSamples;
};

struct PassStats {
  const char *name = "";
  bool rebuilt = false;
  double startSeconds = 0;
  double graphLoadSeconds = 0;
  double closureSeconds = 0;
  double parseSeconds = 0;
  double fetchSeconds = 0;
  /** Parts of `fetchSeconds`: the tree fetch and node mapping, the batched type request
   * with its interning, and turning answers into rows. */
  double mapSeconds = 0;
  double prefetchSeconds = 0;
  double rowSeconds = 0;
  double replaySeconds = 0;
  double writeSeconds = 0;
  double totalSeconds = 0;
  int files = 0;
  int fresh = 0;
  int missing = 0;
  int stale = 0;
  size_t nodesQueried = 0;
  size_t nodeTypesWritten = 0;
  size_t nodeTypesReplayed = 0;
  size_t typesInGraph = 0;
  size_t symbolsInGraph = 0;
  types::FactsStats facts;
  tsgo::RpcStats rpc;
  size_t peakRssBytes = 0;
  size_t dbBytes = 0;
  UnmappedReport *unmappedReport = nullptr;
};

using Clock = std::chrono::steady_clock;

double since(Clock::time_point start)
{
  return std::chrono::duration<double>(Clock::now() - start).count();
}

size_t peakRss()
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) {
    return counters.PeakWorkingSetSize;
  }
  return 0;
#else
  rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
#ifdef __APPLE__
  return size_t(usage.ru_maxrss);
#else
  return size_t(usage.ru_maxrss) * 1024;
#endif
#endif
}

size_t fileSize(const std::string &path)
{
  std::error_code ec;
  auto size = std::filesystem::file_size(path, ec);
  return ec ? 0 : size_t(size);
}

void removeDatabase(const std::string &path)
{
  std::error_code ec;
  for (const char *suffix : {"", "-wal", "-shm"}) {
    std::filesystem::remove(path + suffix, ec);
  }
}

std::string lower(std::string text)
{
  for (char &c : text) {
    c = char(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

/** Project source files under the project directory: no libs, no packages, no `.d.ts`. */
void selectFiles(const Vector<string> &names,
                 const std::string &projectDir,
                 size_t limit,
                 Vector<std::string> &files)
{
  std::string prefix = lower(projectDir);
  for (const string &name : names) {
    std::string path(name.c_str(), name.size());
    std::string folded = lower(path);
    if (!folded.starts_with(prefix) ||
        folded.find("/node_modules/") != std::string::npos || folded.ends_with(".d.ts") ||
        !isSourceFile(path))
    {
      continue;
    }
    files.append(path);
    if (limit && files.size() >= limit) {
      break;
    }
  }
}

/** Maps the file's expression nodes with the same table `TypeFacts` uses and tallies the
 * misses. */
bool reportUnmapped(tsgo::Session &session,
                    const tsgo::Client &client,
                    const std::string &path,
                    const std::string &bytes,
                    const ast::AstFile &astFile,
                    UnmappedReport &report,
                    string &error)
{
  tsgo::EncodedSourceFile encoded;
  if (!session.sourceFile(path, encoded, error)) {
    return false;
  }
  tsgo::NodeIndexTable table;
  table.build(astFile, encoded);
  size_t misses = 0;
  Vector<std::string> samples;
  for (const ast::PreorderEntry &entry : astFile.preorder()) {
    const ast::Node *node = entry.node;
    if (!node->isExpression() || node->end <= node->start || table.lookup(node) >= 0) {
      continue;
    }
    misses++;
    report.byKind[int(node->kind)]++;
    if (samples.size() < 12) {
      size_t length = node->end - node->start;
      std::string excerpt = bytes.substr(node->start, length > 40 ? 40 : length);
      for (char &c : excerpt) {
        if (c == '\n' || c == '\r') {
          c = ' ';
        }
      }
      char line[160];
      std::snprintf(line,
                    sizeof line,
                    "%s %u-%u `%s`",
                    ast::kindName(node->kind),
                    node->start,
                    node->end,
                    excerpt.c_str());
      samples.append(std::string(line));
      if (samples.size() <= 4) {
        // tsgo nodes ending near ours show whether the offsets are merely shifted.
        for (int i = 0; i < int(encoded.nodes.size()); i++) {
          const tsgo::EncodedNode &n = encoded.nodes[i];
          if (n.kind != 0 && n.kind != tsgo::kNodeListKind && n.end + 6 >= node->end &&
              n.end <= node->end + 6)
          {
            std::snprintf(line,
                          sizeof line,
                          "  tsgo #%d kind %u pos %u end %u",
                          i,
                          n.kind,
                          n.pos,
                          n.end);
            samples.append(std::string(line));
          }
        }
      }
    }
  }
  report.total += misses;
  if (misses > report.worstCount) {
    report.worstCount = misses;
    report.worstFile = path;
    report.worstSamples = samples;
  }
  (void)client;
  return true;
}

void printUnmapped(const UnmappedReport &report)
{
  std::printf("unmapped expression nodes: %zu\n", report.total);
  for (int round = 0; round < 12; round++) {
    int best = -1;
    for (int kind = 0; kind < kKindCount; kind++) {
      if (report.byKind[kind] > 0 &&
          (best < 0 || report.byKind[kind] > report.byKind[best]))
      {
        best = kind;
      }
    }
    if (best < 0) {
      break;
    }
    std::printf("  %-28s %d\n", ast::kindName(ast::NodeKind(best)), report.byKind[best]);
    const_cast<UnmappedReport &>(report).byKind[best] = 0;
  }
  if (!report.worstFile.empty()) {
    std::printf("  worst file %s (%zu):\n", report.worstFile.c_str(), report.worstCount);
    for (const std::string &sample : report.worstSamples) {
      std::printf("    %s\n", sample.c_str());
    }
  }
}

bool runPass(const Options &options, PassStats &stats, string &error)
{
  auto passStart = Clock::now();
  std::string projectDir =
      std::filesystem::path(options.tsconfig).parent_path().generic_string();

  // The server first, since its version keys the store.
  auto started = Clock::now();
  tsgo::Client client;
  tsgo::ClientOptions clientOptions;
  clientOptions.cwd = string(projectDir.c_str());
  string exe;
  if (tsgo::resolveTsgoExe(std::filesystem::current_path().generic_string(), exe)) {
    clientOptions.exe = exe;
  }
  tsgo::SnapshotInfo snapshot;
  if (!client.start(clientOptions, error) ||
      !client.openProject(options.tsconfig.c_str(), snapshot, error))
  {
    return false;
  }
  if (snapshot.projects.size() == 0) {
    error = string("the server opened no project for the tsconfig");
    return false;
  }
  tsgo::Session session(
      client, snapshot.id, std::string_view(snapshot.projects[0].id.c_str()));
  Vector<string> names;
  if (!session.sourceFileNames(names, error)) {
    return false;
  }
  stats.startSeconds = since(started);

  Vector<std::string> files;
  selectFiles(names, projectDir, options.limit, files);
  stats.files = int(files.size());

  cache::Store store;
  cache::StoreOptions storeOptions;
  storeOptions.path = string(options.cache.c_str());
  storeOptions.tsgoVersion = client.version();
  if (!store.open(storeOptions, error)) {
    return false;
  }
  stats.rebuilt = store.rebuilt();

  types::TypeGraph graph;
  started = Clock::now();
  if (!store.loadGraph(graph, error)) {
    return false;
  }
  stats.graphLoadSeconds = since(started);
  cache::GraphCursor cursor{graph.typeCount(), graph.symbolCount()};

  started = Clock::now();
  cache::ImportGraph imports;
  cache::DiskFileSystem fs;
  for (const std::string &file : files) {
    if (!cache::loadClosure(imports, file, fs, error)) {
      return false;
    }
  }
  stats.closureSeconds = since(started);

  std::string tsconfigBytes;
  readFile(options.tsconfig, tsconfigBytes);
  cache::FileCache fileCache(store, imports, cache::hashContent(tsconfigBytes));

  for (const std::string &path : files) {
    std::string bytes;
    if (!readFile(path, bytes)) {
      error = string("cannot read ");
      error += path.c_str();
      return false;
    }
    started = Clock::now();
    syntax::Diagnostics diagnostics;
    syntax::GrammarTree tree;
    ast::AstFile astFile(&tree);
    syntax::Parser parser(std::string_view(bytes), optionsFor(path), diagnostics);
    parser.parseFile(tree);
    ast::lower(tree, astFile);
    stats.parseSeconds += since(started);

    if (options.unmapped && stats.unmappedReport &&
        !reportUnmapped(
            session, client, path, bytes, astFile, *stats.unmappedReport, error))
    {
      return false;
    }

    cache::FileRecord record;
    cache::Freshness freshness;
    if (!fileCache.lookup(path, record, freshness, error)) {
      return false;
    }
    if (freshness == cache::Freshness::Fresh) {
      stats.fresh++;
      started = Clock::now();
      Vector<cache::NodeType> replayed;
      if (!store.getNodeTypes(record.contentHash, replayed, error)) {
        return false;
      }
      stats.nodeTypesReplayed += replayed.size();
      stats.replaySeconds += since(started);
      continue;
    }
    if (freshness == cache::Freshness::Missing) {
      stats.missing++;
    } else {
      stats.stale++;
    }

    started = Clock::now();
    types::TypeFacts facts(session, graph);
    if (!facts.beginFile(astFile, path, error)) {
      return false;
    }
    Vector<const ast::Node *> nodes;
    for (const ast::PreorderEntry &entry : astFile.preorder()) {
      if (entry.node->isExpression() && entry.node->end > entry.node->start) {
        nodes.append(entry.node);
      }
    }
    // The first query builds the node table; time it apart from the batch.
    if (nodes.size() > 0) {
      facts.typeOf(nodes[0]);
    }
    auto mapped = Clock::now();
    stats.mapSeconds += std::chrono::duration<double>(mapped - started).count();
    facts.prefetch(span<const ast::Node *const>(nodes.data(), nodes.size()));
    auto prefetched = Clock::now();
    stats.prefetchSeconds += std::chrono::duration<double>(prefetched - mapped).count();
    Vector<cache::NodeType> rows;
    for (const ast::Node *node : nodes) {
      types::TypeId type = facts.typeOf(node);
      if (!type) {
        continue;
      }
      cache::NodeType row;
      row.start = node->start;
      row.end = node->end;
      row.kind = uint32_t(node->kind);
      row.typeHash = graph.type(type).hash;
      rows.append(row);
    }
    stats.nodesQueried += nodes.size();
    const types::FactsStats &fileStats = facts.stats();
    stats.facts.nodeHits += fileStats.nodeHits;
    stats.facts.nodeMisses += fileStats.nodeMisses;
    stats.facts.unmappedNodes += fileStats.unmappedNodes;
    stats.facts.typeFetches += fileStats.typeFetches;
    stats.facts.childFetches += fileStats.childFetches;
    stats.facts.symbolFetches += fileStats.symbolFetches;
    facts.endFile();
    stats.rowSeconds += since(prefetched);
    stats.fetchSeconds += since(started);

    started = Clock::now();
    if (!store.begin(error) ||
        !store.putNodeTypes(record.contentHash,
                            span<const cache::NodeType>(rows.data(), rows.size()),
                            error) ||
        !store.saveGraph(graph, cursor, error) || !fileCache.commitFile(record, error) ||
        !store.commit(error))
    {
      store.rollback();
      return false;
    }
    stats.nodeTypesWritten += rows.size();
    stats.writeSeconds += since(started);
  }

  stats.typesInGraph = graph.typeCount();
  stats.symbolsInGraph = graph.symbolCount();
  stats.rpc = client.stats();
  string ignored;
  session.release(ignored);
  client.stop();
  store.close();
  stats.dbBytes = fileSize(options.cache) + fileSize(options.cache + "-wal");
  stats.peakRssBytes = peakRss();
  stats.totalSeconds = since(passStart);
  return true;
}

void printText(const PassStats &s)
{
  std::printf("%s pass%s\n", s.name, s.rebuilt ? " (store rebuilt)" : "");
  std::printf("  files %d: fresh %d, missing %d, stale %d\n",
              s.files,
              s.fresh,
              s.missing,
              s.stale);
  std::printf("  time %.2fs: tsgo start %.2f, graph load %.2f, closure %.2f, parse %.2f, "
              "fetch %.2f, replay %.2f, write %.2f\n",
              s.totalSeconds,
              s.startSeconds,
              s.graphLoadSeconds,
              s.closureSeconds,
              s.parseSeconds,
              s.fetchSeconds,
              s.replaySeconds,
              s.writeSeconds);
  std::printf("  fetch parts: map %.2f, prefetch %.2f, rows %.2f; pipe wait %.2f\n",
              s.mapSeconds,
              s.prefetchSeconds,
              s.rowSeconds,
              s.rpc.readSeconds);
  std::printf("  nodes queried %zu, node types written %zu, replayed %zu\n",
              s.nodesQueried,
              s.nodeTypesWritten,
              s.nodeTypesReplayed);
  std::printf(
      "  graph: %zu types, %zu symbols; fetches: %d types, %d children, %d symbols; "
      "%d nodes unmapped\n",
      s.typesInGraph,
      s.symbolsInGraph,
      s.facts.typeFetches,
      s.facts.childFetches,
      s.facts.symbolFetches,
      s.facts.unmappedNodes);
  std::printf("  rpc: %d calls, %d callbacks, %.1f MB sent, %.1f MB received\n",
              s.rpc.calls,
              s.rpc.callbacks,
              double(s.rpc.bytesSent) / 1e6,
              double(s.rpc.bytesReceived) / 1e6);
  std::printf("  peak rss %.1f MB, database %.1f MB\n",
              double(s.peakRssBytes) / 1e6,
              double(s.dbBytes) / 1e6);
}

void printJson(const PassStats &s, bool last)
{
  std::printf(
      "  \"%s\": {\"rebuilt\": %s, \"files\": %d, \"fresh\": %d, \"missing\": %d, "
      "\"stale\": %d, \"seconds\": {\"total\": %.3f, \"tsgoStart\": %.3f, "
      "\"graphLoad\": %.3f, \"closure\": %.3f, \"parse\": %.3f, \"fetch\": %.3f, "
      "\"replay\": %.3f, \"write\": %.3f, \"map\": %.3f, \"prefetch\": %.3f, "
      "\"rows\": %.3f, \"pipeWait\": %.3f}, \"nodesQueried\": %zu, "
      "\"nodeTypesWritten\": %zu, \"nodeTypesReplayed\": %zu, \"types\": %zu, "
      "\"symbols\": %zu, \"typeFetches\": %d, \"childFetches\": %d, "
      "\"symbolFetches\": %d, \"unmappedNodes\": %d, \"rpcCalls\": %d, "
      "\"bytesSent\": %zu, \"bytesReceived\": %zu, \"peakRssBytes\": %zu, "
      "\"dbBytes\": %zu}%s\n",
      s.name,
      s.rebuilt ? "true" : "false",
      s.files,
      s.fresh,
      s.missing,
      s.stale,
      s.totalSeconds,
      s.startSeconds,
      s.graphLoadSeconds,
      s.closureSeconds,
      s.parseSeconds,
      s.fetchSeconds,
      s.replaySeconds,
      s.writeSeconds,
      s.mapSeconds,
      s.prefetchSeconds,
      s.rowSeconds,
      s.rpc.readSeconds,
      s.nodesQueried,
      s.nodeTypesWritten,
      s.nodeTypesReplayed,
      s.typesInGraph,
      s.symbolsInGraph,
      s.facts.typeFetches,
      s.facts.childFetches,
      s.facts.symbolFetches,
      s.facts.unmappedNodes,
      s.rpc.calls,
      s.rpc.bytesSent,
      s.rpc.bytesReceived,
      s.peakRssBytes,
      s.dbBytes,
      last ? "" : ",");
}

} // namespace

int cacheBenchCommand(int argc, char **argv)
{
  Options options;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
      options.cache = argv[++i];
    } else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
      options.limit = size_t(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--keep") == 0) {
      options.keep = true;
    } else if (std::strcmp(argv[i], "--json") == 0) {
      options.json = true;
    } else if (std::strcmp(argv[i], "--unmapped") == 0) {
      options.unmapped = true;
    } else {
      options.tsconfig = argv[i];
    }
  }
  if (options.tsconfig.empty()) {
    std::fprintf(stderr,
                 "usage: lintrix cache-bench [--cache <db>] [--limit N] [--keep] "
                 "[--json] [--unmapped] "
                 "<tsconfig>\n");
    return 2;
  }
  std::error_code ec;
  options.tsconfig = std::filesystem::absolute(options.tsconfig, ec).generic_string();
  if (options.cache.empty()) {
    std::filesystem::create_directories(".cache", ec);
    options.cache = ".cache/cache-bench.db";
  }
  if (!options.keep) {
    removeDatabase(options.cache);
  }

  UnmappedReport unmapped;
  PassStats cold;
  cold.name = "cold";
  cold.unmappedReport = &unmapped;
  PassStats warm;
  warm.name = "warm";
  string error;
  if (!runPass(options, cold, error)) {
    std::fprintf(stderr, "cold pass failed: %s\n", error.c_str());
    return 1;
  }
  if (!runPass(options, warm, error)) {
    std::fprintf(stderr, "warm pass failed: %s\n", error.c_str());
    return 1;
  }
  if (options.json) {
    std::printf("{\n  \"tsconfig\": \"%s\",\n", options.tsconfig.c_str());
    printJson(cold, false);
    printJson(warm, true);
    std::printf("}\n");
  } else {
    printText(cold);
    printText(warm);
    if (options.unmapped) {
      printUnmapped(unmapped);
    }
  }
  return 0;
}

} // namespace fastlint::cli
