#include "vcd_variable.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>

#include "vcd_utils.h"

namespace vcd {
using namespace utils;

namespace {
constexpr std::array<const char*, size_t(VariableType::_count_)> VAR_TYPES{
    "wire", "reg", "string", "parameter", "integer", "real", "realtime", "time", "event",
    "supply0", "supply1", "tri", "triand", "trior", "trireg", "tri0", "tri1", "wand", "wor" };

// *size* or the default size of *type*
unsigned size_of(VariableType type, unsigned size, const std::string &name)
{
    switch (type)
    {
    case VariableType::integer:
    case VariableType::time:
    case VariableType::real:
    case VariableType::realtime: return size ? size : 64;
    case VariableType::string:   return size ? size : 1;
    case VariableType::event:    return 1;
    default:
        if (!size)
            throw TypeException{ std::format("Must supply size for type '{}' of var '{}'",
                                                VAR_TYPES[size_t(type)], name) };
        return size;
    }
}

Variable::Kind kind_of(VariableType type, unsigned size)
{
    using Kind = Variable::Kind;
    switch (type)
    {
    case VariableType::integer:
    case VariableType::time:     return (size == 1) ? Kind::scalar : Kind::vector;
    case VariableType::real:
    case VariableType::realtime: return Kind::real;
    case VariableType::string:   return Kind::string;
    case VariableType::event:    return Kind::event;
    default:                     return Kind::vector;
    }
}
}

// -----------------------------
Variable::Variable(std::string var_name, VariableType var_type, unsigned var_size, unsigned var_ident) :
    name(std::move(var_name)),
    type(var_type),
    size(size_of(var_type, var_size, name)),
    ident(var_ident),
    kind(kind_of(var_type, size))
{}

// -----------------------------
std::string Variable::declaration() const
{
    return std::format("$var {} {} {:x} {} $end", VAR_TYPES[size_t(type)], size, ident, name);
}

// -----------------------------
void Variable::change_record(const VarValue &value, VarValue &record) const
{
    switch (kind)
    {
    case Kind::scalar:
    case Kind::event:
    {   // "<bit>", an empty value is 'x'
        const char c = value.empty() ? char(Bit::undef) : to_lower(value[0]);
        if (value.size() > 1 || !is_bit(c))
            throw TypeException{ std::format("Invalid scalar value '{}'", value) };
        record.assign(1, c);
        return;
    }
    case Kind::vector:
    {   // "b<size bits> ", an empty value is all 'x'; a shorter value is left-extended
        // with 'x'/'z' if its leftmost bit is 'x'/'z', with '0' otherwise (IEEE 1364-2005, 18.2.1)
        if (value.size() > size)
            throw TypeException{ std::format("Invalid binary vector value '{}' size '{}'", value, size) };
        const char first = value.empty() ? char(Bit::undef) : to_lower(value[0]);
        const char fill = (first == Bit::undef || first == Bit::highz) ? first : char(Bit::zero);
        record.resize(size_t(size) + 2);
        char *p = record.data();
        *p++ = 'b';
        p = std::fill_n(p, size - value.size(), fill);
        for (const char ch : value)
        {
            const char c = to_lower(ch);
            if (!is_bit(c))
                throw TypeException{ std::format("Invalid binary vector value '{}' size '{}'", value, size) };
            *p++ = c;
        }
        *p = ' ';
        return;
    }
    case Kind::real:
    {   // "r<number> "
        double d = 0.0;
        size_t idx = 0;
        try { d = std::stod(value, &idx); }
        catch (const std::exception&) { idx = 0; }
        if (idx == 0 || idx != value.size())
            throw TypeException{ std::format("Invalid real value '{}'", value) };
        record.clear();
        std::format_to(std::back_inserter(record), "r{:.16g} ", d);
        return;
    }
    case Kind::string:
    {   // "s<string> " (GTKWave extension), non-empty, without whitespace
        if (value.empty() || !std::all_of(value.begin(), value.end(), is_graph))
            throw TypeException{ std::format("Invalid string value '{}'", value) };
        record.assign(1, 's');
        record += value;
        record += ' ';
        return;
    }
    }
}
}
