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

  /** Starts the server and opens every tsconfig in `tsconfigs` in one snapshot; `tsc`
   * is resolved from the first tsconfig's directory, then from the working directory.
   * Files are typed only after `setFileProject` routes them to one of these. */
  bool open(const Vector<string> &tsconfigs, string &error);
  /** Routes `file` to `tsconfig` (which `open` must have loaded) for the queries run on
   * it. Both are made canonical. A file with no route is typed syntactically only. */
  void setFileProject(std::string_view file, std::string_view tsconfig);
  void close();
  bool isOpen() const
  {
    return m_open;
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

  /** One loaded tsconfig: the id every query passes, and its parsed options (whose
   * strictness `TypeFacts::strictOption` reads). */
  struct Project {
    string id;
    /** The `parseConfigFile` answer; owned here, so its `options` node outlives a bind.
     */
    tsgo::JsonDocument *config = nullptr;
  };

  bool readFile(std::string_view path, string &content) override;
  int fileExists(std::string_view path) override;
  bool newSnapshot(const tsgo::SnapshotUpdate &update, string &error);
  void bindSession(const Project &project);

  tsgo::Client m_client;
  tsgo::SnapshotInfo m_snapshot;
  bool m_open = false;
  /** Every loaded tsconfig, keyed by the hash of its canonical path. */
  Map<uint64_t, Project> m_projects;
  /** A file's canonical-path hash to the canonical-path hash of its tsconfig. */
  Map<uint64_t, uint64_t> m_fileProject;
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
