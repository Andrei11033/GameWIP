/// @file io_test.cpp
/// @brief Executable self-tests for the IO library.
///
/// The suite exercises public transfer contracts, partial progress, limits,
/// seeking, error propagation, and custom Reader/Writer implementations.

#include "validation/tests/io/io_test.h"

#include "io/io.h"
#include "validation/tests/internal/helpers/runner_helpers.h"
#include "test_support/test_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;

    using GameWIP::Test::IOTestOptions;
    using IO::Types::ErrorCode;

#include "validation/tests/io/io_fixtures.inl"

    /// ------------------------------------------------------------
    /// Compile-time contracts
    /// ------------------------------------------------------------

    // Fixture and memory-stream ownership
    static_assert(std::is_nothrow_move_constructible_v<MinimalReader>);
    static_assert(std::is_nothrow_move_assignable_v<MinimalReader>);
    static_assert(!std::is_copy_constructible_v<MinimalReader>);
    static_assert(!std::is_copy_assignable_v<MinimalReader>);
    static_assert(std::is_nothrow_move_constructible_v<ChunkedWriter>);
    static_assert(std::is_nothrow_move_assignable_v<ChunkedWriter>);
    static_assert(!std::is_copy_constructible_v<ChunkedWriter>);
    static_assert(!std::is_copy_assignable_v<ChunkedWriter>);
    static_assert(std::is_nothrow_move_constructible_v<IO::MemoryReader>);
    static_assert(std::is_nothrow_move_assignable_v<IO::MemoryReader>);
    static_assert(std::is_nothrow_move_constructible_v<IO::MemoryWriter>);
    static_assert(std::is_nothrow_move_assignable_v<IO::MemoryWriter>);

    // Flush-mode validation
    static_assert(IO::isValidFlushMode(IO::Types::FlushMode::None));
    static_assert(IO::isValidFlushMode(IO::Types::FlushMode::Data));
    static_assert(IO::isValidFlushMode(IO::Types::FlushMode::DataAndMetadataBestEffort));
    static_assert(!IO::isValidFlushMode(static_cast<IO::Types::FlushMode>(-1)));
    static_assert(noexcept(IO::isValidFlushMode(IO::Types::FlushMode::None)));

    // Non-throwing operation contracts
    static_assert(noexcept(IO::makeStatus(ErrorCode::Unknown)));
    static_assert(noexcept(std::declval<IO::Reader &>().isOpen()));
    static_assert(noexcept(std::declval<IO::Reader &>().canSeek()));
    static_assert(noexcept(std::declval<IO::Reader &>().read(std::declval<std::span<std::byte>>())));
    static_assert(noexcept(std::declval<IO::Reader &>().close()));
    static_assert(noexcept(std::declval<IO::Reader &>().position()));
    static_assert(noexcept(std::declval<IO::Reader &>().size()));
    static_assert(noexcept(std::declval<IO::Reader &>().seek(0, IO::Types::SeekOrigin::Begin)));
    static_assert(noexcept(std::declval<IO::Writer &>().isOpen()));
    static_assert(noexcept(std::declval<IO::Writer &>().canSeek()));
    static_assert(noexcept(std::declval<IO::Writer &>().write(std::declval<std::span<const std::byte>>())));
    static_assert(noexcept(std::declval<IO::Writer &>().flush()));
    static_assert(noexcept(std::declval<IO::Writer &>().close()));
    static_assert(noexcept(std::declval<IO::Writer &>().position()));
    static_assert(noexcept(std::declval<IO::Writer &>().seek(0, IO::Types::SeekOrigin::Begin)));
    static_assert(noexcept(IO::readAllBytes(std::declval<IO::Reader &>())));
    static_assert(noexcept(IO::readAllText(std::declval<IO::Reader &>())));
    static_assert(noexcept(IO::writeAllBytes(std::declval<IO::Writer &>(), std::declval<std::span<const std::byte>>())));
    static_assert(noexcept(IO::writeAllText(std::declval<IO::Writer &>(), std::declval<std::string_view>())));
    static_assert(noexcept(std::declval<IO::MemoryWriter &>().reserve(0)));
    static_assert(noexcept(std::declval<const IO::MemoryWriter &>().copyText()));

    // Borrowed source and result types
    static_assert(std::is_same_v<decltype(std::declval<const IO::MemoryWriter &>().copyText()), IO::Types::CopyTextResult>);
    static_assert(std::is_constructible_v<IO::MemoryReader, std::string &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::string &&>);
    static_assert(std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &>);
    static_assert(!std::is_constructible_v<IO::MemoryReader, std::vector<std::byte> &&>);
    static_assert(std::is_same_v<
                  decltype(IO::writeAllBytes(std::declval<IO::Writer &>(), std::declval<std::span<const std::byte>>())),
                  IO::Types::WriteResult>);
    static_assert(std::is_same_v<decltype(IO::writeAllText(std::declval<IO::Writer &>(), std::declval<std::string_view>())), IO::Types::WriteResult>);

    // Focused suite declarations keep cross-suite calls independent of fragment include order.

    /// ------------------------------------------------------------
    /// Test suites
    /// ------------------------------------------------------------

    void testErrorCodeNames(TestSupport::Context &context);
    void testStatusAndDefaultContracts(TestSupport::Context &context);
    void testMemoryReader(TestSupport::Context &context);
    void testMemoryReaderSeek(TestSupport::Context &context);
    void testMemoryWriter(TestSupport::Context &context);
    void testReadAllBytes(TestSupport::Context &context);
    void testReadAllText(TestSupport::Context &context);
    void testWriteAllBytes(TestSupport::Context &context);
    void testWriteAllText(TestSupport::Context &context);

#include "validation/tests/io/contracts_test.inl"
#include "validation/tests/io/memory_test.inl"
#include "validation/tests/io/read_test.inl"
#include "validation/tests/io/write_test.inl"
} // namespace

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Test runner
    /// ------------------------------------------------------------

    int runIOTests([[maybe_unused]] int argc, [[maybe_unused]] char *const *argv, const IOTestOptions &options)
    {
        const TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::Internal::RunnerHelpers::makeReportingOptions(options);

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("IO test options: report={}", options.writeReport ? options.reportPath.string() : std::string_view{"disabled"}));

        runner.runSuite("IO error code names", testErrorCodeNames);
        runner.runSuite("IO status defaults", testStatusAndDefaultContracts);
        runner.runSuite("IO MemoryReader", testMemoryReader);
        runner.runSuite("IO MemoryReader seek", testMemoryReaderSeek);
        runner.runSuite("IO MemoryWriter", testMemoryWriter);
        runner.runSuite("IO readAllBytes", testReadAllBytes);
        runner.runSuite("IO readAllText", testReadAllText);
        runner.runSuite("IO writeAllBytes", testWriteAllBytes);
        runner.runSuite("IO writeAllText", testWriteAllText);

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("IO library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return GameWIP::Test::Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
