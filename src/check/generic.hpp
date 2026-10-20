#pragma once

#include "check/check.hpp"

#include <string>
#include <vector>

namespace tlc {

bool same_spec(const SpecArg &lhs, const SpecArg &rhs);
int param_index(const std::vector<TypeParam> &params, const std::string &name);
SpecArg pack_bound(std::vector<SpecArg> parts);
std::string generic_param_key(const FunctionDecl &fn, const std::vector<TypeParam> &params, bool skip_receiver);
std::string generic_full_key(const FunctionDecl &fn, const std::vector<TypeParam> &params);
std::string generic_list_suffix(const FunctionDecl &fn);

} // namespace tlc
