#include <iostream>
#include <bitset>
#include "vcd_writer.h"
using namespace vcd;

int main()
{
    HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, utils::now());
    std::string filename = "dump.vcd";
    VCDWriter writer(filename, head);
    VarPtr counter_var = writer.register_var("a.b.c", "counter", VariableType::integer, 8);
    VarPtr var_var = writer.register_var("a.b", "var", VariableType::integer, 8);
    for (int timestamp = 0; timestamp < 5; ++timestamp)
    {
        const int c_val = 10 + timestamp * 2;
        const int v_val = 11 + timestamp * 2;
        writer.change(counter_var, timestamp, std::bitset<8>(c_val).to_string());
        writer.change(var_var, timestamp, std::bitset<8>(v_val).to_string());
    }
    return 0;
}

