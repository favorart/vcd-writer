#include <fstream>
#include <vcd_writer.h>
#include <gtest/gtest.h>

using namespace vcd;

// -----------------------------

// Read the contents to the output file
static std::string read_file()
{
    std::ifstream file("test.vcd");
    EXPECT_TRUE(file.is_open());
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return contents;
}

// -----------------------------

// Test case for invalid timescale quan
TEST(VCDHeaderTest, TimeScaleQuanInvalid)
{
    EXPECT_THROW(makeVCDHeader(TimeScale::_COUNT_), VCDTypeException);
}

// Test case for invalid timescale unit
TEST(VCDHeaderTest, TimeScaleUnitInvalid)
{
    EXPECT_THROW(makeVCDHeader(TimeScale::ONE, TimeScaleUnit::_count_), VCDTypeException);
}

// -----------------------------

// Test case for initializing VCDWriter
TEST(VCDWriterTest, Constructor)
{
    HeadPtr header;
    // empty date
    EXPECT_NO_THROW(header = makeVCDHeader(TimeScale::HUNDRED, TimeScaleUnit::ms, "", "comment", "version"));
    EXPECT_NE(header, nullptr);
    EXPECT_NO_THROW(VCDWriter("test.vcd", header));

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 100 ms $end\n"
              "$comment comment $end\n"
              "$version version $end\n"
              "$enddefinitions $end\n");
}

// -----------------------------

class VCDWriterFixture : public ::testing::Test
{
protected:
    virtual void SetUp() { writer = std::make_shared<VCDWriter>("test.vcd", header); }
    virtual void TearDown() {}

    HeadPtr header = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, "2024-05-21 22:16:16");
    WriterPtr writer;

    const std::string scope = "top";
    const std::string name = "my_var";
    const std::string next_name = "next_var";
    const unsigned size = 32;
};

// -----------------------------

// Test case for flushing VCDWriter
TEST_F(VCDWriterFixture, Flush)
{
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    EXPECT_NE(contents.find("$enddefinitions"), std::string::npos);
}

// Test case for closing VCDWriter
TEST_F(VCDWriterFixture, Close)
{
    writer->close();
    EXPECT_THROW(writer->flush(), vcd::VCDPhaseException);
}

// Test case for changing variable after VCDWriter is closed
TEST_F(VCDWriterFixture, ChangeAfterClose)
{
    auto var = writer->register_var(scope, name);
    writer->close();
    EXPECT_THROW(writer->change(var, 10, "1"), vcd::VCDPhaseException);
}

// Test case for aliasing variable after VCDWriter is closed
TEST_F(VCDWriterFixture, AliasAfterClose)
{
    auto var = writer->register_var(scope, name);
    writer->close();
    EXPECT_THROW(writer->register_var(scope, name), vcd::VCDPhaseException);
}

// Test case for no scopes defined
TEST_F(VCDWriterFixture, NoScopes)
{
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    EXPECT_EQ(contents.find("$scope"), std::string::npos);
}

// Test case for one variable
TEST_F(VCDWriterFixture, OneVar)
{
    auto var = writer->register_var(scope, name);
    writer->change(var, 0, "1");
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    EXPECT_NE(contents.find(name), std::string::npos);
    EXPECT_NE(contents.find("1"), std::string::npos);
}

TEST_F(VCDWriterFixture, RegisterVariables)
{
    // No such a var
    EXPECT_THROW(writer->var(scope, name), VCDException);
    // Register a variable
    VarPtr var = writer->register_var(scope, name);
    // The variable is registered
    EXPECT_NE(var, nullptr);
    // Check for the variable
    EXPECT_EQ(writer->var(scope, name), var);

    // No such a var
    EXPECT_THROW(writer->var(scope, next_name), VCDException);
    // Register a next variable
    VarPtr next_var = writer->register_var(scope, next_name, VariableType::wire, size);
    // The next variable is registered
    EXPECT_NE(next_var, nullptr);
    // Check for the next variable
    EXPECT_EQ(writer->var(scope, next_name), next_var);

    // Change the variable's value
    TimeStamp timestamp = 10;
    VarValue value = { VCDValues::UNDEF };
    bool changed = writer->change(var, timestamp, value);
    // Check that the value is not changed error
    EXPECT_FALSE(changed);

    value = { VCDValues::ONE };
    changed = writer->change(var, timestamp, value);
    // Check that the value is changed correctly
    EXPECT_TRUE(changed);

    // Change the value of the variable
    timestamp = 1000;
    value = "1";
    changed = writer->change(scope, next_name, timestamp, value);
    // Check that the value is changed correctly
    EXPECT_TRUE(changed);

    // Check for a new timestamp < the set timestamp
    timestamp = 10;
    EXPECT_THROW(writer->change(scope, next_name, timestamp, value), VCDException);
}

TEST_F(VCDWriterFixture, ChangeVectorValue)
{
    // Register a vector variable
    VarPtr var = writer->register_var("my_scope", "my_vector", VariableType::wire, 4);
    // Change the value of the variable
    TimeStamp timestamp = 10;
    VarValue value = "0011";
    bool changed = writer->change(var, timestamp, value);
    // Check that the value is changed correctly
    EXPECT_TRUE(changed);
}

TEST_F(VCDWriterFixture, DumpValues)
{
    // Register a variable
    VarPtr var = writer->register_var("my_scope", name, VariableType::wire, 1);
    // Change the value of the variable
    TimeStamp timestamp = 10;
    VarValue value = "1";
    writer->change(var, timestamp, value);

    // Dump the values
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 1 ns $end\n"
        "$date 2024-05-21 22:16:16 $end\n"
        "$scope module my_scope $end\n"
        "$var wire 1 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n"
        "#10\n"
        "b1 0\n");
}

TEST_F(VCDWriterFixture, FlushClose)
{
    // Register a variable
    VarPtr var = writer->register_var("my_scope", name, VariableType::wire, 1);

    // Change the value of the variable
    TimeStamp timestamp = 10;
    VarValue value = "1";
    writer->change(var, timestamp, value);

    // Flush the VCDWriter object
    writer->flush();
    // Close the VCDWriter object
    writer->close();
    // Cannot flush after close
    EXPECT_THROW(writer->flush(), VCDException);

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 1 ns $end\n"
        "$date 2024-05-21 22:16:16 $end\n"
        "$scope module my_scope $end\n"
        "$var wire 1 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n"
        "#10\n"
        "b1 0\n");
}

TEST_F(VCDWriterFixture, SetScopeType)
{
    std::string scope = "my_scope";
    // Add the scope with a var
    VarPtr var = writer->register_var(scope, name, VariableType::wire, 1);

    // Change the scope type to "module"
    writer->set_scope_type(scope, ScopeType::function);
    // Flush the VCDWriter object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 1 ns $end\n"
        "$date 2024-05-21 22:16:16 $end\n"
        "$scope function my_scope $end\n"
        "$var wire 1 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n");
}

TEST_F(VCDWriterFixture, SetScopeDefaultType)
{
    // Set the default scope type to "module"
    writer->set_scope_default_type(ScopeType::fork);
    // Register a variable
    VarPtr var = writer->register_var("my_scope", "my_var", VariableType::wire, 1);
    // Flush the VCDWriter object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 1 ns $end\n"
        "$date 2024-05-21 22:16:16 $end\n"
        "$scope fork my_scope $end\n"
        "$var wire 1 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n");
}

TEST_F(VCDWriterFixture, SetScopeSep)
{
    // Set the scope separator to "/"
    writer->set_scope_sep("/");
    // Register a variable
    VarPtr var = writer->register_var("my_scope/top", "my_var", VariableType::wire, 1);
    // Flush the VCDWriter object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 1 ns $end\n"
        "$date 2024-05-21 22:16:16 $end\n"
        "$scope module my_scope $end\n"
        "$scope module top $end\n"
        "$var wire 1 0 my_var $end\n"
        "$upscope $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n");
}

TEST_F(VCDWriterFixture, DumpOff)
{
    // Register a variable
    VarPtr var = writer->register_var("my_scope", "my_var", VariableType::wire, 2);

    // Change the value of the variable
    TimeStamp timestamp = 10;
    VarValue value = "1";
    writer->change(var, timestamp, value);

    // Dump off
    writer->dump_off(timestamp);

    // Change the value of the variable again
    writer->change(var, timestamp + 1, value);

    // Flush the VCDWriter object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 1 ns $end\n"
        "$date 2024-05-21 22:16:16 $end\n"
        "$scope module my_scope $end\n"
        "$var wire 2 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bxx 0\n"
        "$end\n"
        "#10\n"
        "b01 0\n"
        "#10\n"
        "$dumpoff\n"
        "bx 0\n"
        "$end\n");
}

TEST_F(VCDWriterFixture, DumpOn)
{
    // Register a variable
    VarPtr var = writer->register_var("my_scope", "my_var", VariableType::wire, 3);

    // Change the value of the variable
    TimeStamp timestamp = 10;
    VarValue value = { VCDValues::ZERO, VCDValues::ZERO, VCDValues::ZERO };
    writer->change(var, timestamp, value);

    // Dump off
    writer->dump_off(timestamp);

    // Change the value of the variable again
    value = { VCDValues::ZERO, VCDValues::ZERO, VCDValues::ONE };
    writer->change(var, timestamp, value);

    // Dump off twice
    writer->dump_off(timestamp + 1);

    // Dump on
    writer->dump_on(timestamp + 1);

    // Change the value of the variable again
    value = { VCDValues::ZERO, VCDValues::ONE, VCDValues::ONE };
    writer->change(var, timestamp + 1, value);

    // Flush the VCDWriter object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 1 ns $end\n"
        "$date 2024-05-21 22:16:16 $end\n"
        "$scope module my_scope $end\n"
        "$var wire 3 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bxxx 0\n"
        "$end\n"
        "#10\n"
        "b000 0\n"
        "#10\n"
        "$dumpoff\n"
        "bx 0\n"
        "$end\n"
        "#11\n"
        "$dumpon\n"
        "#11\n"
        "b011 0\n");
}

// -----------------------------

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
