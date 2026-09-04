#pragma once

#include "fastlint/syntax/tree.h"
#include "util/error.h"

namespace fastlint::syntax {
using litestl::util::StrLiteral;
using litestl::util::ValueOrErrors;

static constexpr StrLiteral ClassMemberExpected = "Class member or property expected";
static constexpr StrLiteral ParseExtendsFailed = "Failed to parse extends clause";
static constexpr StrLiteral ParseMethodFailed = "Class parsing failed";
static constexpr StrLiteral ParseFailure = "Parse failure";

using ParseClassMethodRet =
    ValueOrErrors<NodeId, "Parser::parseMethod", ClassMemberExpected>;
using ParseClassLikeRet = ValueOrErrors<NodeId,
                                        "Parser::parseClassLike",
                                        ParseMethodFailed,
                                        ParseExtendsFailed>;
using ParsePrimaryRet = ValueOrErrors<NodeId, "Parser::parsePrimary", ParseFailure>;
} // namespace fastlint::syntax