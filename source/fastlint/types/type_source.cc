#include "fastlint/types/type_source.h"

#include "fastlint/tsgo/source_file.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fastlint::types {

namespace {

namespace fs = std::filesystem;

string copy(std::string_view text)
{
  string out;
  for (char c : text) {
    out += c;
  }
  return out;
}

std::string_view view(const string &s)
{
  return std::string_view(s.c_str(), s.size());
}

uint64_t hashOf(std::string_view text)
{
  return Hasher().text(text).value;
}

/** `path` made absolute with forward slashes, as the server names files. */
string absolutePath(std::string_view path)
{
  std::error_code ec;
  fs::path p = fs::absolute(fs::path(std::string(path)), ec);
  return copy(p.generic_string());
}

/** Reads `path` into `out`; false when it is not a regular file. */
bool readDisk(std::string_view path, std::string &out)
{
  std::error_code ec;
  fs::path p{std::string(path)};
  if (!fs::is_regular_file(p, ec)) {
    return false;
  }
  FILE *f = std::fopen(p.string().c_str(), "rb");
  if (!f) {
    return false;
  }
  char buffer[65536];
  size_t n;
  while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
    out.append(buffer, n);
  }
  std::fclose(f);
  return true;
}

} // namespace

ProjectTypes::ProjectTypes() = default;

ProjectTypes::~ProjectTypes()
{
  close();
}

bool ProjectTypes::open(const Vector<string> &tsconfigs, string &error)
{
  close();
  if (tsconfigs.isEmpty()) {
    error = string("no tsconfig to open");
    return false;
  }
  Vector<string> configs;
  for (const string &t : tsconfigs) {
    configs.append(absolutePath(view(t)));
  }
  std::string dir =
      fs::path(std::string(view(configs[0]))).parent_path().generic_string();
  tsgo::ClientOptions options;
  options.cwd = copy(dir);
  options.files = this;
  string exe;
  if (tsgo::resolveTsgoExe(dir, exe) || tsgo::resolveTsgoExe(std::string_view(), exe)) {
    options.exe = exe;
  }
  tsgo::SnapshotUpdate update;
  for (const string &config : configs) {
    update.openProjects.append(copy(view(config)));
  }
  if (!m_client.start(options, error) ||
      !m_client.updateSnapshot(update, m_snapshot, error))
  {
    m_client.stop();
    return false;
  }
  if (m_snapshot.projects.isEmpty()) {
    error = string("the server opened no project");
    m_client.stop();
    return false;
  }
  bool caseSensitive = m_client.caseSensitiveFileNames();
  for (const tsgo::ProjectInfo &info : m_snapshot.projects) {
    Project project;
    project.id = info.id;
    // Rules only consult the options for strictness; an unreadable config reads strict.
    auto *doc = litestl::alloc::New<tsgo::JsonDocument>("tsgo config");
    string ignored;
    if (!m_client.parseConfigFile(view(info.configFileName), *doc, ignored)) {
      doc->clear();
    }
    project.config = doc;
    string canonical = tsgo::canonicalPath(view(info.id), caseSensitive);
    m_projects.add_overwrite(hashOf(view(canonical)), std::move(project));
  }
  m_open = true;
  return true;
}

void ProjectTypes::setFileProject(std::string_view file, std::string_view tsconfig)
{
  bool caseSensitive = m_client.caseSensitiveFileNames();
  string fileKey = tsgo::canonicalPath(view(absolutePath(file)), caseSensitive);
  string configKey = tsgo::canonicalPath(view(absolutePath(tsconfig)), caseSensitive);
  m_fileProject.add_overwrite(hashOf(view(fileKey)), hashOf(view(configKey)));
}

void ProjectTypes::close()
{
  endFile();
  if (m_facts) {
    litestl::alloc::Delete(m_facts);
    m_facts = nullptr;
  }
  if (m_session) {
    string error;
    m_session->release(error);
    litestl::alloc::Delete(m_session);
    m_session = nullptr;
  }
  m_graph.clearSessionIds();
  m_client.stop();
  for (auto &entry : m_projects) {
    if (entry.value.config) {
      litestl::alloc::Delete(entry.value.config);
    }
  }
  m_projects = Map<uint64_t, Project>();
  m_fileProject = Map<uint64_t, uint64_t>();
  m_served = Map<uint64_t, Served>();
  m_open = false;
}

void ProjectTypes::bindSession(const Project &project)
{
  if (m_session && m_session->snapshot() == m_snapshot.id &&
      view(m_session->project()) == view(project.id))
  {
    return;
  }
  if (m_facts) {
    m_stats = stats();
    litestl::alloc::Delete(m_facts);
    m_facts = nullptr;
  }
  if (m_session) {
    litestl::alloc::Delete(m_session);
  }
  // `alloc::New` takes its arguments by value, which would copy the client.
  m_session = new (litestl::alloc::alloc("tsgo session", sizeof(tsgo::Session)))
      tsgo::Session(m_client, m_snapshot.id, view(project.id));
  m_facts = new (litestl::alloc::alloc("type facts", sizeof(TypeFacts)))
      TypeFacts(*m_session, m_graph);
  const tsgo::JsonValue *root = project.config ? project.config->root() : nullptr;
  m_facts->setCompilerOptions(root ? root->get("options") : nullptr);
}

bool ProjectTypes::newSnapshot(const tsgo::SnapshotUpdate &update, string &error)
{
  int previous = m_snapshot.id;
  tsgo::SnapshotInfo next;
  if (!m_client.updateSnapshot(update, next, error)) {
    return false;
  }
  m_snapshot = std::move(next);
  // Live tsgo ids belong to the released snapshot.
  m_graph.clearSessionIds();
  string ignored;
  m_client.release(previous, ignored);
  return true;
}

TypeFacts *ProjectTypes::beginFile(const ast::AstFile &file,
                                   std::string_view path,
                                   std::string_view text,
                                   string &error)
{
  endFile();
  if (!m_client.alive()) {
    error = string("type server is not running");
    return nullptr;
  }
  string absolute = absolutePath(path);
  string canonical =
      tsgo::canonicalPath(view(absolute), m_client.caseSensitiveFileNames());
  uint64_t key = hashOf(view(canonical));
  uint64_t hash = hashOf(text);

  // A file no tsconfig claims is typed syntactically only, not a degraded run, so the
  // empty error tells the linter to cache it rather than warn.
  uint64_t *projectKey = m_fileProject.lookup_ptr(key);
  Project *project = projectKey ? m_projects.lookup_ptr(*projectKey) : nullptr;
  if (!project) {
    error = string();
    return nullptr;
  }

  tsgo::SnapshotUpdate update;
  Served *served = m_served.lookup_ptr(key);
  if (!served) {
    Served fresh{canonical, copy(text), hash, false};
    std::string disk;
    if (!readDisk(view(absolute), disk)) {
      // A file the disk lacks joins the inferred project once it is opened.
      update.created.append(absolute);
      update.openFiles.append(absolute);
      fresh.opened = true;
    } else if (hashOf(disk) != hash) {
      update.changed.append(absolute);
    }
    m_served.add_overwrite(key, std::move(fresh));
  } else if (served->hash != hash) {
    served->text = copy(text);
    served->hash = hash;
    if (served->opened) {
      // An open file keeps its text until it is closed; reopen it afterwards.
      tsgo::SnapshotUpdate close;
      close.closeFiles.append(absolute);
      close.changed.append(absolute);
      if (!newSnapshot(close, error)) {
        return nullptr;
      }
      update.openFiles.append(absolute);
    } else {
      update.changed.append(absolute);
    }
  }
  if ((!update.created.isEmpty() || !update.changed.isEmpty() ||
       !update.openFiles.isEmpty()) &&
      !newSnapshot(update, error))
  {
    return nullptr;
  }
  bindSession(*project);
  if (!m_facts->beginFile(file, view(absolute), error)) {
    return nullptr;
  }
  m_current = absolute;
  return m_facts;
}

FactsStats ProjectTypes::stats() const
{
  FactsStats total = m_stats;
  if (m_facts) {
    const FactsStats &live = m_facts->stats();
    total.nodeHits += live.nodeHits;
    total.nodeMisses += live.nodeMisses;
    total.unmappedNodes += live.unmappedNodes;
    total.typeFetches += live.typeFetches;
    total.childFetches += live.childFetches;
    total.symbolFetches += live.symbolFetches;
  }
  return total;
}

void ProjectTypes::endFile()
{
  if (m_facts) {
    m_facts->endFile();
  }
  m_current = string();
}

bool ProjectTypes::readFile(std::string_view path, string &content)
{
  string canonical = tsgo::canonicalPath(path, m_client.caseSensitiveFileNames());
  Served *served = m_served.lookup_ptr(hashOf(view(canonical)));
  if (served) {
    content = served->text;
    return true;
  }
  // A file we hold no override for (the tsconfig, lib and package files) is read from
  // disk, so a query like `parseConfigFile` that routes through the provider still sees
  // it.
  std::string disk;
  if (!readDisk(path, disk)) {
    return false;
  }
  content = copy(disk);
  return true;
}

int ProjectTypes::fileExists(std::string_view path)
{
  string canonical = tsgo::canonicalPath(path, m_client.caseSensitiveFileNames());
  if (m_served.contains(hashOf(view(canonical)))) {
    return 1;
  }
  std::error_code ec;
  return fs::is_regular_file(fs::path(std::string(path)), ec) ? 1 : -1;
}

} // namespace fastlint::types
