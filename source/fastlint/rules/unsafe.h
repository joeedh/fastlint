#pragma once

// Helpers the `no-unsafe-*` rules share: the `any` flow questions typescript-eslint
// keeps in its type-utils package, over `TypeFacts`.

#include "fastlint/ast/node.h"
#include "fastlint/types/type_facts.h"
#include "util/pool.h"
#include "util/string.h"
#include "util/vector.h"

#include <string_view>

namespace fastlint::rules::unsafe {

using litestl::util::Pool;
using litestl::util::string;
using litestl::util::Vector;
using types::TypeFacts;
using types::TypeId;

/** The `this` a call or member chain starts from (`this`, `this.a.b`, `this.a()`), or
 * null. */
ast::Node *thisExpressionOf(ast::Node *node);

/** A type parameter's base constraint, or the type itself. */
TypeId constrained(TypeFacts &facts, TypeId type);

/** Union members of `type`, or `type` itself. */
void unionParts(TypeFacts &facts, TypeId type, Vector<TypeId, 4> &out);

/** `any[]` or `readonly any[]`. */
bool isAnyArray(TypeFacts &facts, TypeId type);
bool isUnknownArray(TypeFacts &facts, TypeId type);

struct UnsafePair {
  TypeId sender = 0;
  TypeId receiver = 0;
};

/**
 * Whether assigning `sender` to `receiver` loses type safety: `any` into anything but
 * `unknown` or `any`, or two instantiations of one generic whose arguments do. `new
 * Map()` with no arguments is exempt, since its constructor is typed `Map<any, any>`.
 * `senderNode` may be null.
 */
bool isUnsafeAssignment(TypeFacts &facts,
                        TypeId sender,
                        TypeId receiver,
                        const ast::Node *senderNode,
                        UnsafePair &out);

enum class AnyKind { Safe, Any, AnyArray, PromiseAny };

/** Which flavour of `any` a type carries; a promise of `any` counts through its
 * awaited type. */
AnyKind discriminateAny(TypeFacts &facts, TypeId type);

/** Owns the text of dynamic placeholders until the linter has interpolated them. */
class Texts {
public:
  std::string_view intern(const string &value)
  {
    string *text = m_texts.alloc();
    *text = value;
    return std::string_view(text->c_str(), text->size());
  }
  /** "error typed", or the type's text in backticks. */
  std::string_view describe(TypeFacts &facts, TypeId type);
  /** The type's text in backticks. */
  std::string_view quoted(TypeFacts &facts, TypeId type);

private:
  Pool<string, 4> m_texts;
};

} // namespace fastlint::rules::unsafe
