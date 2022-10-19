#pragma once

#include <cstdint>
#include <ostream>

struct VarID {
    typedef uint32_t IDType;
    static constexpr uint32_t FREEBITS = 31;
    static constexpr uint32_t IDMASK = (uint32_t(1) << FREEBITS) - 1;
    enum class VarType : uint32_t {
        Parameter = 0x0,
        LoopInductionVariable = 0x1,
        // Memory = 0x2,
        // Term = 0x3
    };

    IDType id;
    VarID(IDType id) : id(id) {}
    VarID(IDType i, VarType typ)
        : id((static_cast<IDType>(typ) << FREEBITS) | i) {}
    bool operator<(VarID x) const { return id < x.id; }
    bool operator<=(VarID x) const { return id <= x.id; }
    bool operator==(VarID x) const { return id == x.id; }
    bool operator>(VarID x) const { return id > x.id; }
    bool operator>=(VarID x) const { return id >= x.id; }
    std::strong_ordering operator<=>(VarID x) { return id <=> x.id; }
    IDType getID() const { return id & IDMASK; }
    VarType getType() const { return static_cast<VarType>(id >> FREEBITS); }
    std::pair<VarType, IDType> getTypeAndId() const {
        return std::make_pair(getType(), getID());
    }
    bool isParam() { return getType() == VarType::Parameter; }
    bool isIndVar() { return getType() == VarType::LoopInductionVariable; }
};
std::ostream &operator<<(std::ostream &os, VarID::VarType s) {
    switch (s) {
    case VarID::VarType::Parameter:
        os << "Constant";
        break;
    case VarID::VarType::LoopInductionVariable:
        os << "Induction Variable";
        break;
        // case VarType::Memory:
        //     os << "Memory";
        //     break;
        // case VarType::Term:
        //     os << "Term";
        //     break;
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, VarID s) {
    return os << s.getType() << ": " << s.getID();
}
