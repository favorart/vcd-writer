#include <fstream>
#include <vcd_writer.h>
#include <gtest/gtest.h>

using namespace vcd;

// -----------------------------

// Read the contents of the output file
static std::string read_file(const std::string &filename = "test.vcd")
{
    // text mode: on Windows the writer emits CRLF line endings
    std::ifstream file(filename);
    EXPECT_TRUE(file.is_open());
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return contents;
}

// Each test writes its own file, so tests are independent of each other
static std::string test_filename()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "_" + info->name() + ".vcd";
}

static const std::string HEADER = "$timescale 1 ns $end\n"
                                  "$date 2024-05-21 22:16:16 $end\n";

// -----------------------------

// Test case for invalid timescale quan
TEST(HeaderTest, TimeScaleQuanInvalid)
{
    EXPECT_THROW(make_header(TimeScale::_count_), TypeException);
    EXPECT_THROW(make_header(TimeScale(5)), TypeException);
    EXPECT_THROW(make_header(TimeScale(0)), TypeException);
}

// Test case for invalid timescale unit
TEST(HeaderTest, TimeScaleUnitInvalid)
{
    EXPECT_THROW(make_header(TimeScale::one, TimeScaleUnit::_count_), TypeException);
    EXPECT_THROW(make_header(TimeScale::one, TimeScaleUnit(-1)), TypeException);
}

// "$end" inside a header value would terminate the section early
TEST(HeaderTest, EndKeywordInjection)
{
    EXPECT_THROW(make_header(TimeScale::one, TimeScaleUnit::ns, "2024 $end"), TypeException);
    EXPECT_THROW(make_header(TimeScale::one, TimeScaleUnit::ns, "", "x $end $var"), TypeException);
    EXPECT_THROW(make_header(TimeScale::one, TimeScaleUnit::ns, "", "", "$end"), TypeException);
}

// The date is "YYYY-MM-DD hh:mm:ss" with a valid calendar date and time, or empty
TEST(HeaderTest, DateFormat)
{
    for (const char *date : { "", "2024-05-21 22:16:16", "2024-02-29 00:00:00", "1999-12-31 23:59:60" })
        EXPECT_NO_THROW(make_header(TimeScale::one, TimeScaleUnit::ns, date)) << date;
    EXPECT_NO_THROW(make_header(TimeScale::one, TimeScaleUnit::ns)); // utils::now()
    for (const char *date : { "today", "2024-05-21", "2024-05-21T22:16:16", "2024-05-21 22:16:16 ",
                              "2024/05/21 22:16:16", "2024-5-21 22:16:16", "2024-05-21 2a:16:16",
                              "2024-13-01 00:00:00", "2024-00-10 00:00:00", "2023-02-29 00:00:00",
                              "2024-04-31 00:00:00", "2024-05-21 24:00:00", "2024-05-21 23:60:00",
                              "2024-05-21 23:59:61", "+024-05-21 22:16:16" })
        EXPECT_THROW(make_header(TimeScale::one, TimeScaleUnit::ns, date), TypeException) << date;
}

// Regression: multi-line comments used to overflow the heap buffer
TEST(HeaderTest, MultiLineComment)
{
    const std::string filename = test_filename();
    std::string comment = "a\nb\r\nc\rd";
    std::string expected = "a\n\tb\n\tc\n\td";
    for (int i = 0; i < 50; ++i)
    {
        comment += "\nline";
        expected += "\n\tline";
    }
    {
        Writer writer(filename, make_header(TimeScale::ten, TimeScaleUnit::us, "", comment));
    }
    EXPECT_EQ(read_file(filename), "$timescale 10 us $end\n"
                                   "$comment " + expected + " $end\n"
                                   "$enddefinitions $end\n");
}

// -----------------------------

// Test case for initializing Writer
TEST(WriterTest, Constructor)
{
    HeadPtr header;
    // empty date
    EXPECT_NO_THROW(header = make_header(TimeScale::hundred, TimeScaleUnit::ms, "", "comment", "version"));
    EXPECT_NE(header, nullptr);
    EXPECT_NO_THROW(Writer("test.vcd", header));

    // Read the contents to the output file
    const std::string contents = read_file();

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, "$timescale 100 ms $end\n"
              "$comment comment $end\n"
              "$version version $end\n"
              "$enddefinitions $end\n");
}

// A null header is replaced by the default one
TEST(WriterTest, NullHeader)
{
    const std::string filename = test_filename();
    {
        HeadPtr header;
        Writer writer(filename, header);
    }
    EXPECT_EQ(read_file(filename).rfind("$timescale 1 ns $end\n", 0), 0u);
}

// Initial timestamp is 64-bit
TEST(WriterTest, InitTimestamp64)
{
    const std::string filename = test_filename();
    const TimeStamp ts = TimeStamp(1) << 40;
    {
        Writer writer(filename, make_header(TimeScale::one, TimeScaleUnit::ns, ""), ts);
        auto var = writer.register_var("top", "v", VariableType::wire, 1, "0");
        writer.change(var, ts + 1, "1");
    }
    EXPECT_EQ(read_file(filename), "$timescale 1 ns $end\n"
                                   "$scope module top $end\n"
                                   "$var wire 1 0 v $end\n"
                                   "$upscope $end\n"
                                   "$enddefinitions $end\n"
                                   "#1099511627776\n"
                                   "$dumpvars\n"
                                   "b0 0\n"
                                   "$end\n"
                                   "#1099511627777\n"
                                   "b1 0\n");
}

// -----------------------------

class WriterFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        filename = test_filename();
        writer = std::make_shared<Writer>(filename, header);
    }
    void TearDown() override {}

    std::string contents()
    {
        writer->close();
        return read_file(filename);
    }

    HeadPtr header = make_header(TimeScale::one, TimeScaleUnit::ns, "2024-05-21 22:16:16");
    WriterPtr writer;
    std::string filename;

    const std::string scope = "top";
    const std::string name = "my_var";
    const std::string next_name = "next_var";
    const unsigned size = 32;
};

// -----------------------------

// Test case for flushing Writer
TEST_F(WriterFixture, Flush)
{
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    EXPECT_NE(contents.find("$enddefinitions"), std::string::npos);
}

// Test case for closing Writer
TEST_F(WriterFixture, Close)
{
    writer->close();
    EXPECT_THROW(writer->flush(), vcd::PhaseException);
    EXPECT_THROW(writer->dump_off(1), vcd::PhaseException);
    EXPECT_THROW(writer->dump_on(1), vcd::PhaseException);
    EXPECT_NO_THROW(writer->close());
}

// Test case for changing variable after Writer is closed
TEST_F(WriterFixture, ChangeAfterClose)
{
    auto var = writer->register_var(scope, name);
    writer->close();
    EXPECT_THROW(writer->change(var, 10, "1"), vcd::PhaseException);
}

// Test case for aliasing variable after Writer is closed
TEST_F(WriterFixture, AliasAfterClose)
{
    auto var = writer->register_var(scope, name);
    writer->close();
    EXPECT_THROW(writer->register_var(scope, name), vcd::PhaseException);
}

// Test case for no scopes defined
TEST_F(WriterFixture, NoScopes)
{
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    EXPECT_EQ(contents.find("$scope"), std::string::npos);
}

// Test case for one variable
TEST_F(WriterFixture, OneVar)
{
    auto var = writer->register_var(scope, name);
    writer->change(var, 0, "1");
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    EXPECT_NE(contents.find(name), std::string::npos);
    EXPECT_NE(contents.find("1"), std::string::npos);
}

TEST_F(WriterFixture, RegisterVariables)
{
    // No such a var
    EXPECT_THROW(writer->var(scope, name), Exception);
    // Register a variable
    VarPtr var = writer->register_var(scope, name);
    // The variable is registered
    EXPECT_NE(var, nullptr);
    // Check for the variable
    EXPECT_EQ(writer->var(scope, name), var);

    // No such a var
    EXPECT_THROW(writer->var(scope, next_name), Exception);
    // Register a next variable
    VarPtr next_var = writer->register_var(scope, next_name, VariableType::wire, size);
    // The next variable is registered
    EXPECT_NE(next_var, nullptr);
    // Check for the next variable
    EXPECT_EQ(writer->var(scope, next_name), next_var);

    // Change the variable's value
    TimeStamp timestamp = 10;
    VarValue value = { Bit::undef };
    bool changed = writer->change(var, timestamp, value);
    // Check that the value is not changed error
    EXPECT_FALSE(changed);

    value = { Bit::one };
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
    EXPECT_THROW(writer->change(scope, next_name, timestamp, value), Exception);
}

TEST_F(WriterFixture, ChangeVectorValue)
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

TEST_F(WriterFixture, DumpValues)
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
    const std::string contents = read_file(filename);

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, HEADER +
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

TEST_F(WriterFixture, FlushClose)
{
    // Register a variable
    VarPtr var = writer->register_var("my_scope", name, VariableType::wire, 1);

    // Change the value of the variable
    TimeStamp timestamp = 10;
    VarValue value = "1";
    writer->change(var, timestamp, value);

    // Flush the Writer object
    writer->flush();
    // Close the Writer object
    writer->close();
    // Cannot flush after close
    EXPECT_THROW(writer->flush(), Exception);

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, HEADER +
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

// flush() with a timestamp writes it once
TEST_F(WriterFixture, FlushTimestamp)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1, "0");
    TimeStamp ts = 20;
    writer->flush(&ts);
    writer->change(var, 20, "1");
    ts = 5; // past timestamps are ignored by flush
    writer->flush(&ts);

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var wire 1 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "b0 0\n"
        "$end\n"
        "#20\n"
        "b1 0\n");
}

TEST_F(WriterFixture, SetScopeType)
{
    // Add the scope with a var
    VarPtr var = writer->register_var("my_scope", name, VariableType::wire, 1);

    // Change the scope type to "function" (a string literal is accepted)
    writer->set_scope_type("my_scope", ScopeType::function);
    EXPECT_THROW(writer->set_scope_type("no_scope", ScopeType::function), PhaseException);
    // Flush the Writer object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, HEADER +
        "$scope function my_scope $end\n"
        "$var wire 1 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n");
}

TEST_F(WriterFixture, SetScopeDefaultType)
{
    // Set the default scope type to "module"
    writer->set_scope_default_type(ScopeType::fork);
    // Register a variable
    VarPtr var = writer->register_var("my_scope", "my_var", VariableType::wire, 1);
    // Flush the Writer object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, HEADER +
        "$scope fork my_scope $end\n"
        "$var wire 1 0 my_var $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n");
}

TEST_F(WriterFixture, SetScopeSep)
{
    // Set the scope separator to "/"
    writer->set_scope_sep("/");
    // Register a variable
    VarPtr var = writer->register_var("my_scope/top", "my_var", VariableType::wire, 1);
    // Flush the Writer object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, HEADER +
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

// Regression: a parent scope was closed and re-opened before its child
TEST_F(WriterFixture, NestedScopes)
{
    writer->register_var("a.b.c", "counter", VariableType::integer, 8);
    writer->register_var("a.b", "var", VariableType::integer, 8);

    EXPECT_EQ(contents(), HEADER +
        "$scope module a $end\n"
        "$scope module b $end\n"
        "$var integer 8 1 var $end\n"
        "$scope module c $end\n"
        "$var integer 8 0 counter $end\n"
        "$upscope $end\n"
        "$upscope $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bxxxxxxxx 0\n"
        "bxxxxxxxx 1\n"
        "$end\n");
}

// Regression: scopes sharing a textual prefix ("a", "ab", "a-c") were nested wrongly
TEST_F(WriterFixture, ScopesWithCommonPrefix)
{
    writer->register_var("a", "x", VariableType::wire, 1);
    writer->register_var("a.b", "y", VariableType::wire, 1);
    writer->register_var("ab.c", "z", VariableType::wire, 1);
    writer->register_var("a-c", "q", VariableType::wire, 1);

    EXPECT_EQ(contents(), HEADER +
        "$scope module a $end\n"
        "$var wire 1 0 x $end\n"
        "$scope module b $end\n"
        "$var wire 1 1 y $end\n"
        "$upscope $end\n"
        "$upscope $end\n"
        "$scope module a-c $end\n"
        "$var wire 1 3 q $end\n"
        "$upscope $end\n"
        "$scope module ab $end\n"
        "$scope module c $end\n"
        "$var wire 1 2 z $end\n"
        "$upscope $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "bx 1\n"
        "bx 2\n"
        "bx 3\n"
        "$end\n");
}

TEST_F(WriterFixture, DumpOff)
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

    // Flush the Writer object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, HEADER +
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
        "$dumpoff\n"
        "bx 0\n"
        "$end\n");
}

// Regression: "$dumpon" was written without values and "$end"
TEST_F(WriterFixture, DumpOn)
{
    // Register a variable
    VarPtr var = writer->register_var("my_scope", "my_var", VariableType::wire, 3);

    // Change the value of the variable
    TimeStamp timestamp = 10;
    VarValue value = { Bit::zero, Bit::zero, Bit::zero };
    writer->change(var, timestamp, value);

    // Dump off
    writer->dump_off(timestamp);

    // Change the value of the variable again
    value = { Bit::zero, Bit::zero, Bit::one };
    writer->change(var, timestamp, value);

    // Dump off twice
    writer->dump_off(timestamp + 1);

    // Dump on
    writer->dump_on(timestamp + 1);

    // Change the value of the variable again
    value = { Bit::zero, Bit::one, Bit::one };
    writer->change(var, timestamp + 1, value);

    // Dump on twice: no-op
    writer->dump_on(timestamp + 2);

    // Flush the Writer object
    writer->flush();

    // Read the contents to the output file
    const std::string contents = read_file(filename);

    // Assert that the contents of the file are correct
    EXPECT_EQ(contents, HEADER +
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
        "$dumpoff\n"
        "bx 0\n"
        "$end\n"
        "#11\n"
        "$dumpon\n"
        "b001 0\n"
        "$end\n"
        "b011 0\n");
}

// Regression: dump_on() during registration wrote "$dumpon" before the header
TEST_F(WriterFixture, DumpOnDuringRegistration)
{
    writer->dump_on(0);
    writer->register_var("s", "v", VariableType::wire, 1);

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var wire 1 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n");
}

// Regression: dump_off() before the first change produced "$dumpvars" without "$end"
TEST_F(WriterFixture, DumpOffDuringRegistration)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1);
    writer->dump_off(0);
    writer->change(var, 1, "1");
    writer->dump_on(2);

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var wire 1 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n"
        "$dumpoff\n"
        "bx 0\n"
        "$end\n"
        "#2\n"
        "$dumpon\n"
        "b1 0\n"
        "$end\n");
}

// Regression: dump_off()/dump_on() did not track time, so timestamps could go backwards
TEST_F(WriterFixture, DumpOffTimestampOrder)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1);
    writer->change(var, 1, "1");
    writer->dump_off(5);
    EXPECT_THROW(writer->change(var, 3, "0"), PhaseException);
    EXPECT_THROW(writer->dump_on(4), PhaseException);
    EXPECT_NO_THROW(writer->dump_on(5));
}

// Regression: null var was dereferenced before the null check
TEST_F(WriterFixture, NullVar)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1);
    writer->change(var, 5, "1");
    EXPECT_THROW(writer->change(VarPtr{}, 1, "1"), TypeException);
    EXPECT_THROW(writer->change(VarPtr{}, 10, "1"), TypeException);
}

// A variable registered in another writer is rejected
TEST_F(WriterFixture, ForeignVar)
{
    Writer other(filename + ".other.vcd", make_header());
    VarPtr foreign = other.register_var("s", "v", VariableType::wire, 1);
    writer->register_var("s", "v", VariableType::wire, 1);
    EXPECT_THROW(writer->change(foreign, 1, "1"), TypeException);
}

// Regression: last two characters of a vector value were not validated,
// an empty value threw, and short 'x'/'z' values were left-extended with '0'
TEST_F(WriterFixture, VectorValues)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 4, "0");
    EXPECT_THROW(writer->change(var, 1, "0012"), TypeException);
    EXPECT_THROW(writer->change(var, 1, "7"), TypeException);
    EXPECT_THROW(writer->change(var, 1, "01-"), TypeException);
    EXPECT_THROW(writer->change(var, 1, "00000"), TypeException);
    writer->change(var, 1, "");
    writer->change(var, 2, "1Z");
    writer->change(var, 3, "xz");
    writer->change(var, 4, "Z1");

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var wire 4 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "b0000 0\n"
        "$end\n"
        "#1\n"
        "bxxxx 0\n"
        "#2\n"
        "b001z 0\n"
        "#3\n"
        "bxxxz 0\n"
        "#4\n"
        "bzzz1 0\n");
}

// Scalar: an empty value is 'x', anything else must be a single 4-state char
TEST_F(WriterFixture, ScalarValues)
{
    VarPtr var = writer->register_var("s", "v", VariableType::integer, 1, "0");
    EXPECT_THROW(writer->change(var, 1, "10"), TypeException);
    EXPECT_THROW(writer->change(var, 1, "2"), TypeException);
    EXPECT_TRUE(writer->change(var, 1, ""));
    EXPECT_TRUE(writer->change(var, 2, "Z"));

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var integer 1 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "00\n"
        "$end\n"
        "#1\n"
        "x0\n"
        "#2\n"
        "z0\n");
}

// Regression: a rejected duplicate left a "ghost" value sharing the next var's ident
TEST_F(WriterFixture, DuplicateVar)
{
    writer->register_var("s", "a", VariableType::wire, 1, "1");
    EXPECT_THROW(writer->register_var("s", "a", VariableType::wire, 1, "0"), TypeException);
    // a rejected initial value does not leave anything behind either
    EXPECT_THROW(writer->register_var("t", "c", VariableType::wire, 1, "7"), TypeException);
    writer->register_var("s", "b", VariableType::wire, 1, "z");

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var wire 1 0 a $end\n"
        "$var wire 1 1 b $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "b1 0\n"
        "bz 1\n"
        "$end\n");
}

// Regression: event variables could not be changed at all
TEST_F(WriterFixture, EventVar)
{
    VarPtr ev = writer->register_var("s", "ev", VariableType::event);
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1, "0");
    EXPECT_TRUE(writer->change(ev, 5, "1"));
    EXPECT_TRUE(writer->change(ev, 5, "1")); // events are never deduplicated
    writer->dump_off(6);

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var event 1 0 ev $end\n"
        "$var wire 1 1 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "b0 1\n"
        "$end\n"
        "#5\n"
        "10\n"
        "10\n"
        "#6\n"
        "$dumpoff\n"
        "bx 1\n"
        "$end\n");
}

// Regression: realtime was written as a bit vector and accepted "1.5" as bits
TEST_F(WriterFixture, RealVars)
{
    VarPtr rt = writer->register_var("s", "rt", VariableType::realtime);
    VarPtr r = writer->register_var("s", "r", VariableType::real);
    EXPECT_THROW(writer->change(rt, 1, "abc"), TypeException);
    EXPECT_THROW(writer->change(rt, 1, "1.5x"), TypeException);
    EXPECT_THROW(writer->change(r, 1, ""), TypeException);
    writer->change(rt, 1, "1.5");
    writer->change(r, 1, "-2");

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var realtime 64 0 rt $end\n"
        "$var real 64 1 r $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "r0 0\n"
        "r0 1\n"
        "$end\n"
        "#1\n"
        "r1.5 0\n"
        "r-2 1\n");
}

// Regression: string values could inject arbitrary lines into the VCD
TEST_F(WriterFixture, StringValues)
{
    VarPtr s = writer->register_var("s", "str", VariableType::string);
    EXPECT_THROW(writer->change(s, 1, "a\n#999"), TypeException);
    EXPECT_THROW(writer->change(s, 1, "a b"), TypeException);
    EXPECT_THROW(writer->change(s, 1, "a\tb"), TypeException);
    EXPECT_THROW(writer->change(s, 1, ""), TypeException);
    writer->change(s, 1, "hello");
    writer->dump_off(2);

    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var string 1 0 str $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "sx 0\n"
        "$end\n"
        "#1\n"
        "shello 0\n"
        "#2\n"
        "$dumpoff\n"
        "$end\n");
}

// Regression: scope and var names could inject arbitrary declarations into the VCD
TEST_F(WriterFixture, InvalidNames)
{
    EXPECT_THROW(writer->register_var("s", "n $end\n$var wire 1 ! evil", VariableType::wire, 1), TypeException);
    EXPECT_THROW(writer->register_var("s", "a b", VariableType::wire, 1), TypeException);
    EXPECT_THROW(writer->register_var("s", "$end", VariableType::wire, 1), TypeException);
    EXPECT_THROW(writer->register_var("s\nx", "v", VariableType::wire, 1), TypeException);
    EXPECT_THROW(writer->register_var("a..b", "v", VariableType::wire, 1), TypeException);
    EXPECT_THROW(writer->register_var(".a", "v", VariableType::wire, 1), TypeException);
    EXPECT_THROW(writer->register_var("a.", "v", VariableType::wire, 1), TypeException);
    EXPECT_THROW(writer->register_var("a.$scope", "v", VariableType::wire, 1), TypeException);
    EXPECT_NO_THROW(writer->register_var("a.b", "data[7:0]", VariableType::wire, 8));
}

// Registration is not allowed after the first value change at a later time
TEST_F(WriterFixture, RegisterAfterDumping)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1);
    writer->change(var, 1, "1");
    EXPECT_THROW(writer->register_var("s", "w", VariableType::wire, 1), PhaseException);
}

// The destructor closes the writer and never throws
TEST_F(WriterFixture, DestructorCloses)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1);
    writer->change(var, 1, "1");
    writer.reset();
    EXPECT_EQ(read_file(filename), HEADER +
        "$scope module s $end\n"
        "$var wire 1 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n"
        "#1\n"
        "b1 0\n");
}

// Output larger than the writer's buffer is written completely and in order
TEST_F(WriterFixture, LargeOutput)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 64);
    std::string expected = HEADER +
        "$scope module s $end\n"
        "$var wire 64 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "b" + std::string(64, 'x') + " 0\n"
        "$end\n";
    for (TimeStamp t = 1; t <= 5000; ++t) // ~350 KB
    {
        const std::string value(64, (t & 1) ? '1' : '0');
        EXPECT_TRUE(writer->change(var, t, value));
        expected += "#" + std::to_string(t) + "\nb" + value + " 0\n";
    }
    EXPECT_EQ(contents(), expected);
}

// The buffered output reaches the file on flush()
TEST_F(WriterFixture, FlushWritesBufferedOutput)
{
    VarPtr var = writer->register_var("s", "v", VariableType::wire, 1);
    writer->change(var, 1, "1");
    TimeStamp ts = 2;
    writer->flush(&ts);
    EXPECT_EQ(read_file(filename), HEADER +
        "$scope module s $end\n"
        "$var wire 1 0 v $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bx 0\n"
        "$end\n"
        "#1\n"
        "b1 0\n"
        "#2\n");
}

// Deduplication keeps working while the value buffers are reused between vars
TEST_F(WriterFixture, ValueBuffersReuse)
{
    VarPtr a = writer->register_var("s", "a", VariableType::wire, 4);
    VarPtr b = writer->register_var("s", "b", VariableType::wire, 4);
    EXPECT_TRUE(writer->change(a, 1, "1010"));
    EXPECT_TRUE(writer->change(b, 1, "0101"));
    EXPECT_FALSE(writer->change(a, 2, "1010"));
    EXPECT_FALSE(writer->change(b, 2, "0101"));
    EXPECT_TRUE(writer->change(a, 3, "0101"));
    EXPECT_TRUE(writer->change(b, 3, "1010"));
    EXPECT_FALSE(writer->change(a, 4, "101"));   // left-extended to "0101"
    EXPECT_THROW(writer->change(b, 4, "10a0"), TypeException);
    EXPECT_FALSE(writer->change(b, 4, "1010"));  // a failed change does not alter the value
    EXPECT_FALSE(writer->change("s", "a", 5, "0101"));
    EXPECT_TRUE(writer->change("s", "b", 5, "1111"));
    EXPECT_EQ(contents(), HEADER +
        "$scope module s $end\n"
        "$var wire 4 0 a $end\n"
        "$var wire 4 1 b $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "$dumpvars\n"
        "bxxxx 0\n"
        "bxxxx 1\n"
        "$end\n"
        "#1\n"
        "b1010 0\n"
        "b0101 1\n"
        "#3\n"
        "b0101 0\n"
        "b1010 1\n"
        "#5\n"
        "b1111 1\n");
}

// Characters are checked as ASCII, independently of the C locale
TEST_F(WriterFixture, NonAsciiValues)
{
    EXPECT_THROW(writer->register_var("s", "v\xC3\xA9", VariableType::wire, 1), TypeException);
    VarPtr str = writer->register_var("s", "str", VariableType::string, 0, "a");
    VarPtr vec = writer->register_var("s", "vec", VariableType::wire, 2);
    EXPECT_THROW(writer->change(str, 1, "\xC3\xA9"), TypeException);
    EXPECT_THROW(writer->change(vec, 1, "\xD8" "1"), TypeException);
    EXPECT_TRUE(writer->change(vec, 1, "XZ"));
}

// -----------------------------

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
