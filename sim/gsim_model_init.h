#pragma once
#include <cstddef>
#include <cstring>
#include <type_traits>

// GSIM 93b8cd2 emits a word count as the memset byte count in init().
// Initialize only the generated primitive state, preserving simulator control
// fields and any nontrivial members in a derived test model. See known issues.
template<class Model, class State>
void initializeGsimModel(Model& model, void (State::*initialize)()) {
    static_assert(std::is_base_of_v<State, Model> || std::is_same_v<State, Model>);
    static_assert(std::is_standard_layout_v<State>);
    static_assert(std::is_trivially_copyable_v<State>);
    constexpr auto begin = offsetof(State, _var_start);
    constexpr auto end = offsetof(State, _var_end);
    static_assert(begin < end && end <= sizeof(State));
    auto& state = static_cast<State&>(model);
    auto* bytes = reinterpret_cast<unsigned char*>(&state);
    std::memset(bytes + begin, 0, end - begin);
    (state.*initialize)();
}
