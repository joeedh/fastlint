#pragma once

#include "fastlint/tsgo/client.h"
#include "fastlint/tsgo/queries.h"
#include "fastlint/types/type_facts.h"
#include "fastlint/types/type_graph.h"

#include <string_view>

namespace fastlint::types {

/** Hands the linter a `TypeFacts` for each file it lints; implementations own the
 * server. */
class TypeSource {
public:
  virtual ~TypeSource() = default;

  /** Selects `file`, parsed from `text` and known to the server by `path`, for the
   * queries that follow. Returns null with `error` when the server cannot type the file.
   */
  virtual TypeFacts *beginFile(const ast::AstFile &file,
                               std::string_view path,
                               std::string_view text,
                               string &error) = 0;
  virtual void endFile() = 0;
};

/** One tsgo project answering for the linter's files. The text handed to `beginFile` is
 * what the server checks, so a fixpoint pass sees its own edits and a file that is not on
 * disk can still be typed. */
class ProjectTypes : public TypeSource, private tsgo::FileProvider {
public:
  ProjectTypes();
  ~ProjectTypes() override;
  ProjectTypes(const ProjectTypes &) = delete;
  ProjectTypes &operator=(const ProjectTypes &) = delete;

  /** Starts the server and opens `tsconfig`; `tsc` is resolved from the tsconfig's
   * directory, then from the working directory. */
  bool open(std::string_view tsconfig, string &error);
  void close();
  bool isOpen() const
  {
    return m_facts != nullptr;
  }

  TypeFacts *beginFile(const ast::AstFile &file,
                       std::string_view path,
                       std::string_view text,
                       string &error) override;
  void endFile() override;

  TypeGraph &graph()
  {
    return m_graph;
  }
  /** Query counts summed over every file since `open`. */
  FactsStats stats() const;
  const tsgo::RpcStats &rpcStats() const
  {
    return m_client.stats();
  }

private:
  struct Served {
    string path;
    string text;
    uint64_t hash = 0;
    /** Held open in the server because the disk lacks it. */
    bool opened = false;
  };

  bool readFile(std::string_view path, string &content) override;
  int fileExists(std::string_view path) override;
  bool newSnapshot(const tsgo::SnapshotUpdate &update, string &error);
  bool bindSession(std::string_view file, string &error);

  tsgo::Client m_client;
  tsgo::SnapshotInfo m_snapshot;
  /** The `parseConfigFile` answer for the open tsconfig; its `options` member feeds
   * `TypeFacts::strictOption`. */
  tsgo::JsonDocument m_config;
  tsgo::Session *m_session = nullptr;
  TypeFacts *m_facts = nullptr;
  TypeGraph m_graph;
  FactsStats m_stats;
  /** Text last handed over, keyed by the hash of the canonical path; served to the
   * server's re-reads. */
  Map<uint64_t, Served> m_served;
  string m_current;
};

} // namespace fastlint::types
