/// @file test_support.cpp
/// @brief Core implementation for the GameWIP TestSupport library.

#include "test_support/test_support.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <stdlib.h>
#endif

namespace GameWIP::TestSupport
{
    namespace
    {
        std::mutex environmentMutex;


#if defined(_WIN32)
        [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int inputSize = static_cast<int>(text.size());
            const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, nullptr, 0);
            if (wideSize <= 0)
            {
                std::wstring fallback;
                fallback.reserve(text.size());
                for (char ch : text)
                {
                    const unsigned char value = static_cast<unsigned char>(ch);
                    fallback.push_back(value >= 0x20 && value < 0x80 ? static_cast<wchar_t>(value) : L'?');
                }
                return fallback;
            }

            std::wstring output(static_cast<std::size_t>(wideSize), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, output.data(), wideSize) != wideSize)
            {
                return L"?";
            }
            return output;
        }

        [[nodiscard]] std::string wideToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int inputSize = static_cast<int>(text.size());
            const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, text.data(), inputSize, nullptr, 0, nullptr, nullptr);
            if (utf8Size <= 0)
            {
                return {};
            }

            std::string output(static_cast<std::size_t>(utf8Size), '\0');
            if (WideCharToMultiByte(CP_UTF8, 0, text.data(), inputSize, output.data(), utf8Size, nullptr, nullptr) != utf8Size)
            {
                return {};
            }
            return output;
        }
#endif
        [[nodiscard]] std::optional<std::string> readEnvironmentVariable(std::string_view name)
        {
#if defined(_WIN32)
            const std::wstring nameText = utf8ToWide(name);
            SetLastError(ERROR_SUCCESS);
            const DWORD requiredSize = GetEnvironmentVariableW(nameText.c_str(), nullptr, 0);
            if (requiredSize == 0)
            {
                return GetLastError() == ERROR_ENVVAR_NOT_FOUND
                           ? std::nullopt
                           : std::optional<std::string>{std::string{}};
            }

            std::wstring value(requiredSize, L'\0');
            const DWORD copied = GetEnvironmentVariableW(nameText.c_str(), value.data(), requiredSize);
            if (copied == 0 || copied >= requiredSize)
            {
                return std::nullopt;
            }

            value.resize(copied);
            return wideToUtf8(value);
#else
            const std::string nameText(name);
            if (const char *value = std::getenv(nameText.c_str()))
            {
                return std::string(value);
            }

            return std::nullopt;
#endif
        }

        void setEnvironmentVariableValue(std::string_view name, std::string_view value)
        {
            const std::string nameText(name);
            const std::string valueText(value);
#if defined(_WIN32)
            const std::wstring nameWide = utf8ToWide(name);
            const std::wstring valueWide = utf8ToWide(value);
            _wputenv_s(nameWide.c_str(), valueWide.c_str());
            SetEnvironmentVariableW(nameWide.c_str(), valueWide.c_str());
#else
            setenv(nameText.c_str(), valueText.c_str(), 1);
#endif
        }

        void unsetEnvironmentVariableValue(std::string_view name)
        {
            const std::string nameText(name);
#if defined(_WIN32)
            const std::wstring nameWide = utf8ToWide(name);
            _wputenv_s(nameWide.c_str(), L"");
            SetEnvironmentVariableW(nameWide.c_str(), nullptr);
#else
            unsetenv(nameText.c_str());
#endif
        }

        [[nodiscard]] std::string makeNameReason(std::string_view name, std::string_view reason)
        {
            std::ostringstream message;
            message << name << ": " << reason;
            return message.str();
        }
    }

    namespace Detail
    {
        class ReportSink
        {
        public:
            explicit ReportSink(Types::ReportOptions options)
                : options_(std::move(options))
            {
                if (options_.writeReport && !options_.reportPath.empty())
                {
                    const std::filesystem::path parentPath = options_.reportPath.parent_path();
                    if (!parentPath.empty())
                    {
                        std::error_code error;
                        std::filesystem::create_directories(parentPath, error);
                    }

                    const std::ios::openmode mode = options_.appendReport
                                                        ? (std::ios::out | std::ios::app)
                                                        : (std::ios::out | std::ios::trunc);
                    std::ofstream report(options_.reportPath, mode);
                    reportOpenFailed_ = !report.is_open();
                }
            }

            void write(std::string_view category, std::string_view message)
            {
                write(category, {}, message);
            }

            void write(std::string_view category, std::string_view suiteName, std::string_view message)
            {
                std::ostringstream line;
                line << '[' << category << ']';
                if (!suiteName.empty())
                {
                    line << " [" << suiteName << ']';
                }
                if (!message.empty())
                {
                    line << ' ' << message;
                }

                const std::string text = line.str();
                std::lock_guard lock(mutex_);

                if (options_.writeConsole)
                {
                    std::cout << text << '\n';
                }

                if (options_.writeReport && !options_.reportPath.empty() && !reportOpenFailed_)
                {
                    std::ofstream report(options_.reportPath, std::ios::out | std::ios::app);
                    if (report.is_open())
                    {
                        report << text << '\n';
                    }
                    else
                    {
                        reportOpenFailed_ = true;
                    }
                }
            }

        private:
            Types::ReportOptions options_;
            std::mutex mutex_;
            bool reportOpenFailed_ = false;
        };
    }

    std::size_t Types::Summary::total() const noexcept
    {
        return passed + failed + skipped;
    }

    bool Types::Summary::ok() const noexcept
    {
        return failed == 0;
    }

    bool Types::SuiteResult::ok() const noexcept
    {
        return summary.ok();
    }

    Context::Context(std::string_view suiteName, const Types::ReportOptions &options)
        : Context(suiteName, std::make_shared<Detail::ReportSink>(options))
    {
    }

    Context::Context(std::string_view suiteName, std::shared_ptr<Detail::ReportSink> reportSink)
        : suiteName_(suiteName), reportSink_(std::move(reportSink))
    {
    }

    Context::~Context() = default;

    void Context::info(std::string_view message)
    {
        writeLine("INFO", message);
    }

    void Context::manual(std::string_view instruction)
    {
        writeLine("MANUAL", instruction);
    }

    void Context::metric(std::string_view message)
    {
        writeLine("METRIC", message);
    }

    void Context::stress(std::string_view message)
    {
        writeLine("STRESS", message);
    }

    void Context::summary(std::string_view message)
    {
        writeLine("SUMMARY", message);
    }

    void Context::pass(std::string_view name)
    {
        {
            std::lock_guard lock(mutex_);
            ++summary_.passed;
        }

        writeLine("PASS", name);
    }

    void Context::fail(
        std::string_view name,
        std::string_view reason,
        std::source_location location)
    {
        {
            std::lock_guard lock(mutex_);
            ++summary_.failed;
        }

        writeFailureLine(name, reason, location);
    }

    void Context::skip(std::string_view name, std::string_view reason)
    {
        {
            std::lock_guard lock(mutex_);
            ++summary_.skipped;
        }

        writeLine("SKIP", makeNameReason(name, reason));
    }

    bool Context::expectTrue(
        std::string_view name,
        bool value,
        std::source_location location)
    {
        if (value)
        {
            pass(name);
            return true;
        }

        fail(name, "expected true", location);
        return false;
    }

    bool Context::expectFalse(
        std::string_view name,
        bool value,
        std::source_location location)
    {
        if (!value)
        {
            pass(name);
            return true;
        }

        fail(name, "expected false", location);
        return false;
    }

    bool Context::expectNear(
        std::string_view name,
        double expected,
        double actual,
        double tolerance,
        std::source_location location)
    {
        if (tolerance < 0.0)
        {
            fail(name, "tolerance must be non-negative", location);
            return false;
        }

        if (std::fabs(expected - actual) <= tolerance)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected " << expected << " +/- " << tolerance << ", got " << actual;
        fail(name, reason.str(), location);
        return false;
    }

    bool Context::expectContains(
        std::string_view name,
        std::string_view text,
        std::string_view expectedSubstring,
        std::source_location location)
    {
        if (text.find(expectedSubstring) != std::string_view::npos)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected text to contain '" << expectedSubstring << "'";
        fail(name, reason.str(), location);
        return false;
    }

    bool Context::expectFileContains(
        std::string_view name,
        const std::filesystem::path &path,
        std::string_view expectedSubstring,
        std::source_location location)
    {
        if (fileContains(path, expectedSubstring))
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected file '" << path.string() << "' to contain '" << expectedSubstring << "'";
        fail(name, reason.str(), location);
        return false;
    }

    bool Context::expectFileOccurrenceCount(
        std::string_view name,
        const std::filesystem::path &path,
        std::string_view text,
        std::size_t expectedCount,
        std::source_location location)
    {
        const std::size_t actualCount = countFileOccurrences(path, text);
        if (actualCount == expectedCount)
        {
            pass(name);
            return true;
        }

        std::ostringstream reason;
        reason << "expected " << expectedCount << " occurrences of '" << text
               << "' in file '" << path.string() << "', got " << actualCount;
        fail(name, reason.str(), location);
        return false;
    }

    const std::string &Context::suiteName() const noexcept
    {
        return suiteName_;
    }

    Types::Summary Context::result() const noexcept
    {
        std::lock_guard lock(mutex_);
        return summary_;
    }

    bool Context::ok() const noexcept
    {
        return result().ok();
    }

    void Context::writeLine(std::string_view category, std::string_view message)
    {
        reportSink_->write(category, suiteName_, message);
    }

    void Context::writeFailureLine(
        std::string_view name,
        std::string_view reason,
        const std::source_location &location)
    {
        std::ostringstream message;
        message << name << ": " << reason
                << " (" << location.file_name()
                << ':' << location.line()
                << " in " << location.function_name() << ')';
        writeLine("FAIL", message.str());
    }

    Runner::Runner(Types::ReportOptions options)
        : reportSink_(std::make_shared<Detail::ReportSink>(std::move(options)))
    {
    }

    Runner::~Runner() = default;

    void Runner::info(std::string_view message)
    {
        reportSink_->write("INFO", message);
    }

    void Runner::summary(std::string_view message)
    {
        reportSink_->write("SUMMARY", message);
    }

    Types::Summary Runner::result() const noexcept
    {
        std::lock_guard lock(mutex_);
        return summary_;
    }

    bool Runner::ok() const noexcept
    {
        return result().ok();
    }

    int Runner::exitCode() const noexcept
    {
        return ok() ? 0 : 1;
    }

    void Runner::recordSuiteResult(const Types::SuiteResult &result)
    {
        {
            std::lock_guard lock(mutex_);
            summary_.passed += result.summary.passed;
            summary_.failed += result.summary.failed;
            summary_.skipped += result.summary.skipped;
        }

        std::ostringstream message;
        message << result.name
                << " passed=" << result.summary.passed
                << " failed=" << result.summary.failed
                << " skipped=" << result.summary.skipped
                << " elapsedMs=" << result.elapsedMilliseconds;
        reportSink_->write("RESULT", message.str());
    }

    Section::Section(Context &context, std::string_view name)
        : context_(context), name_(name), timer_(std::make_unique<Timer>())
    {
        std::ostringstream message;
        message << "begin section: " << name_;
        context_.info(message.str());
    }

    Section::~Section() noexcept
    {
        try
        {
            const double elapsedMilliseconds = timer_->elapsedMilliseconds();
            timer_.reset();

            std::ostringstream message;
            message << "section " << name_ << " elapsedMs=" << elapsedMilliseconds;
            context_.metric(message.str());
        }
        catch (...)
        {
        }
    }

    Timer::Timer() noexcept
        : start_(Clock::now())
    {
    }

    void Timer::reset() noexcept
    {
        start_ = Clock::now();
    }

    double Timer::elapsedMilliseconds() const noexcept
    {
        const auto elapsed = Clock::now() - start_;
        return std::chrono::duration<double, std::milli>(elapsed).count();
    }

    double Timer::nanosecondsPerIteration(std::size_t iterations) const noexcept
    {
        if (iterations == 0)
        {
            return 0.0;
        }

        const auto elapsed = Clock::now() - start_;
        const auto nanoseconds = std::chrono::duration<double, std::nano>(elapsed).count();
        return nanoseconds / static_cast<double>(iterations);
    }

    double Types::IterationMetric::nanosecondsPerIteration() const noexcept
    {
        if (iterations == 0)
        {
            return 0.0;
        }

        return (milliseconds * 1'000'000.0) / static_cast<double>(iterations);
    }

    std::string readTextFile(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return {};
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    void writeTextFile(
        const std::filesystem::path &path,
        std::string_view text)
    {
        const std::filesystem::path parentPath = path.parent_path();
        if (!parentPath.empty())
        {
            createDirectories(parentPath);
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            throw std::runtime_error("GameWIP TestSupport could not open text file for writing: " + path.string());
        }

        file << text;
        if (!file)
        {
            throw std::runtime_error("GameWIP TestSupport could not write text file: " + path.string());
        }
    }

    bool fileExists(const std::filesystem::path &path) noexcept
    {
        std::error_code error;
        return std::filesystem::exists(path, error);
    }

    bool fileContains(
        const std::filesystem::path &path,
        std::string_view text)
    {
        if (!fileExists(path))
        {
            return false;
        }

        return readTextFile(path).find(text) != std::string::npos;
    }

    std::size_t countFileOccurrences(
        const std::filesystem::path &path,
        std::string_view text)
    {
        if (text.empty())
        {
            return 0;
        }

        const std::string contents = readTextFile(path);
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = contents.find(text, position)) != std::string::npos)
        {
            ++count;
            position += text.size();
        }
        return count;
    }

    void createDirectories(const std::filesystem::path &path)
    {
        if (!path.empty())
        {
            std::filesystem::create_directories(path);
        }
    }

    void removeIfExists(const std::filesystem::path &path)
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    ScopedEnvironmentVariable::ScopedEnvironmentVariable(std::string_view name, std::string_view value)
        : name_(name)
    {
        std::lock_guard lock(environmentMutex);
        previousValue_ = readEnvironmentVariable(name_);
        setEnvironmentVariableValue(name_, value);
    }

    ScopedEnvironmentVariable::~ScopedEnvironmentVariable()
    {
        std::lock_guard lock(environmentMutex);
        if (previousValue_)
        {
            setEnvironmentVariableValue(name_, *previousValue_);
        }
        else
        {
            unsetEnvironmentVariableValue(name_);
        }
    }

    ScopedUnsetEnvironmentVariable::ScopedUnsetEnvironmentVariable(std::string_view name)
        : name_(name)
    {
        std::lock_guard lock(environmentMutex);
        previousValue_ = readEnvironmentVariable(name_);
        unsetEnvironmentVariableValue(name_);
    }

    ScopedUnsetEnvironmentVariable::~ScopedUnsetEnvironmentVariable()
    {
        std::lock_guard lock(environmentMutex);
        if (previousValue_)
        {
            setEnvironmentVariableValue(name_, *previousValue_);
        }
        else
        {
            unsetEnvironmentVariableValue(name_);
        }
    }

    bool Types::ChildProcessResult::exitedSuccessfully() const noexcept
    {
        return exitCode == 0 && !timedOut && !wasTerminatedByTest;
    }

    bool Types::ChildProcessResult::exitedWithFailure() const noexcept
    {
        return exitCode != 0 || timedOut || wasTerminatedByTest;
    }

    Types::ManualAnswer promptManualCheck(std::string_view question)
    {
        for (;;)
        {
            std::cout << "[MANUAL] " << question << " [y/n/s]: ";
            std::string answer;
            if (!std::getline(std::cin, answer))
            {
                return Types::ManualAnswer::Skipped;
            }

            if (answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes")
            {
                return Types::ManualAnswer::Yes;
            }

            if (answer == "n" || answer == "N" || answer == "no" || answer == "No")
            {
                return Types::ManualAnswer::No;
            }

            if (answer == "s" || answer == "S" || answer == "skip" || answer == "Skip")
            {
                return Types::ManualAnswer::Skipped;
            }
        }
    }

    void StartGate::wait()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this]
                        { return open_; });
    }

    void StartGate::open()
    {
        {
            std::lock_guard lock(mutex_);
            open_ = true;
        }
        condition_.notify_all();
    }

    void StopFlag::requestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
    }

    bool StopFlag::stopRequested() const noexcept
    {
        return stopRequested_.load(std::memory_order_acquire);
    }
}
