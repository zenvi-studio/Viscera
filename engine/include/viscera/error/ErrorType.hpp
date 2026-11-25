#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <viscera/core/FormatString.hpp>

#include "ErrorCode.hpp"
#include "Panic.hpp"

/**
 * @file
 * @brief The one recoverable failure value.
 *
 * A code-only Error stores its code, the static description captured from
 * errorMessage, and the source location of the call that built it. It does
 * not allocate. withContext and wrap push an immutable shared frame.
 * Copying copies the handle and, when a frame exists, bumps a reference
 * count.
 */

namespace viscera
{
    namespace detail
    {
        struct Node;
    } // namespace detail

    /**
     * @brief How many frames the single-line formatter will print.
     *
     * Construction does not cap the chain. The formatter stops after this
     * many frames and appends @c " ...".
     */
    inline constexpr int kMaxErrorFormatFrames = 8;
    inline constexpr std::size_t kMaxErrorBytes = 256;

    /**
     * @brief Recoverable failure: code, static description, location, and an
     *        optional immutable cause chain.
     *
     * @invariant A code-only error has a null cause and an empty message,
     *            and copying it does not allocate.
     * @invariant description() refers to static storage from errorMessage
     *            and stays valid after the Error is destroyed.
     * @invariant The cause chain is acyclic. Each new frame stores the
     *            previous error; it does not point at itself.
     * @invariant Copy and move are noexcept, and sizeof(Error) is at most
     *            kMaxErrorBytes so the logger can capture an Error by value.
     * @invariant Destroying a moved-from Error is safe. Reading it otherwise
     *            is not part of the contract.
     * @note Any exception while attaching a frame, including std::bad_alloc,
     *       calls panic. Any other formatting failure omits the new message
     *       text and keeps the identity rules on withContext and wrap.
     * @see viscera::fail
     * @see viscera::kMaxErrorFormatFrames
     * @see viscera::kMaxErrorBytes
     */
    class Error
    {
        public:
            /**
             * @brief Builds a code-only error.
             *
             * @tparam E Subsystem enum.
             * @param code  Failure identity. Its message is copied as a view.
             * @param where Location of this frame. The default is the caller
             *              of this constructor, not this header.
             * @pre errorMessage(code) points at static storage.
             * @post Does not allocate. cause() is null and message() is empty.
             * @post code() matches @p code, description() is errorMessage(code),
             *       and location() is @p where.
             */
            template <ErrorEnum E>
            explicit Error(E errorCode, std::source_location where = std::source_location::current()) noexcept;

            /**
             * @brief Builds an error with a context message and no cause.
             *
             * @tparam E Subsystem enum.
             * @tparam Args Types checked against @p format.
             * @param code   Failure identity.
             * @param format Compile-time format string. Its location is this
             *               frame's location. It is not captured again inside
             *               the constructor.
             * @param args   Values substituted into @p format.
             * @pre errorMessage(code) points at static storage.
             * @post On success, message() is the formatted text, cause() is
             *       null, and location() is @p format's location.
             * @post If formatting fails for a reason other than allocation,
             *       the result is code-only: the message is omitted and no
             *       frame is stored.
             * @note std::bad_alloc while formatting or allocating the frame
             *       calls panic and does not return.
             */
            template <ErrorEnum E, typename ...Args>
            explicit Error(E errorCode, FormatString<std::type_identity_t<Args> ...> format, Args && ...args) noexcept;

            /**
             * @brief Copies the handle.
             *
             * @param other Error to copy.
             * @post Nothrow. A code-only error stays code-only. A frame is
             *       shared, not deep-copied.
             */
            Error(const Error &other) noexcept;

            /**
             * @brief Moves the handle.
             *
             * @param other Error to move from.
             * @post Nothrow. Destroying @p other afterwards is safe.
             */
            Error(Error &&other) noexcept;

            /**
             * @brief Copy-assigns the handle.
             *
             * @param other Error to copy.
             * @return This error.
             * @post Nothrow. Shares @p other's frame when it has one.
             */
            Error &operator=(const Error &other) noexcept;

            /**
             * @brief Move-assigns the handle.
             *
             * @param other Error to move from.
             * @return This error.
             * @post Nothrow. Destroying @p other afterwards is safe.
             */
            Error &operator=(Error &&other) noexcept;

            /**
             * @brief Releases the handle.
             *
             * @post Destroying a moved-from error is safe.
             */
            ~Error();

            /**
             * @brief Top-level code.
             *
             * @return The domain and value stored on this frame.
             * @post Does not walk the cause chain.
             */
            [[nodiscard]] ErrorCode code() const noexcept { return m_code; }

            /**
             * @brief Static phrase captured from errorMessage.
             *
             * @return View of static storage, not the context message.
             * @post The view remains valid after this Error is destroyed.
             */
            [[nodiscard]] std::string_view description() const noexcept { return m_description; }

            /**
             * @brief True when the top-level code equals @p expected.
             *
             * @param expected Code to compare.
             * @return @c true only when this frame's code matches.
             * @post Does not walk the cause chain.
             */
            [[nodiscard]] bool is(const ErrorCode expected) const noexcept { return m_code == expected; }

            /**
             * @brief True when the top-level code equals @p expected.
             *
             * @tparam E Subsystem enum.
             * @param expected Enumerator to compare.
             * @return @c true only when this frame's domain and value match.
             * @post Does not walk the cause chain.
             */
            template <ErrorEnum E>
            [[nodiscard]] bool is(E expected) const noexcept;

            /**
             * @brief This frame's context text.
             *
             * @return The formatted message, or an empty view when this frame
             *         has none. Not the static description.
             * @post The view is valid for the lifetime of this Error.
             */
            [[nodiscard]] std::string_view message() const noexcept;

            /**
             * @brief Location of this frame.
             *
             * Always present. The rendered line's parenthetical location is
             * the root origin when the chain is within the formatter cap,
             * not necessarily this frame.
             *
             * @return The source location stored for this frame.
             */
            [[nodiscard]] std::source_location location() const noexcept { return m_location; }

            /**
             * @brief Previous error, or null at a root.
             *
             * @return Pointer to the cause stored in this frame, or null when
             *         this error has no cause.
             * @retval nullptr This frame is a root.
             * @retval non-null Previous error. The pointer stays valid for
             *         the lifetime of this Error.
             * @post Does not allocate.
             */
            [[nodiscard]] const Error *cause() const noexcept;

            /**
             * @brief Pushes a frame with the same code and a new message.
             *
             * @tparam Args Types checked against @p format.
             * @param format What this layer was doing.
             * @param args   Values substituted into @p format.
             * @return A new error. This error is unchanged.
             * @post On success, code() and description() are unchanged,
             *       location() is @p format's location, message() is the
             *       formatted text, and cause() points at a copy of this
             *       error.
             * @post If formatting fails for a reason other than allocation,
             *       returns a copy of this error and does not push a frame.
             * @note std::bad_alloc while building the frame calls panic.
             * @see wrap()
             */
            template <typename ...Args>
            [[nodiscard]] Error
                withContext(FormatString<std::type_identity_t<Args> ...> format, Args && ...args) const noexcept;

            /**
             * @brief Pushes a frame with a new code and a new message.
             *
             * Use this when the caller reacts to a different abstraction.
             * Extra detail that does not change the code is withContext.
             *
             * @tparam E Subsystem enum of the new code.
             * @tparam Args Types checked against @p format.
             * @param newCode Code callers should see.
             * @param format  What this layer was doing.
             * @param args    Values substituted into @p format.
             * @return A new error. This error is unchanged and becomes the
             *         cause.
             * @pre errorMessage(newCode) points at static storage.
             * @post On success, code() is @p newCode, location() is
             *       @p format's location, and cause() points at a copy of
             *       this error.
             * @post If formatting fails for a reason other than allocation,
             *       the code still changes and the cause is still linked.
             *       Only the message text is empty.
             * @note std::bad_alloc while linking the cause calls panic.
             * @see withContext()
             */
            template <ErrorEnum E, typename ...Args>
            [[nodiscard]] Error
                wrap(E newCode, FormatString<std::type_identity_t<Args> ...> format, Args && ...args) const noexcept;

        private:
            /// @cond
            Error(
                ErrorCode errorCode,
                std::string_view description,
                std::source_location location,
                std::shared_ptr<const detail::Node> node
            ) noexcept;

            template <ErrorEnum E>
            [[nodiscard]] static ErrorCode toCode(E errorCode) noexcept;

            [[nodiscard]] static Error makeFrame(
                ErrorCode errorCode,
                std::string_view description,
                std::source_location location,
                std::string message,
                std::optional<Error> cause
            );

            [[nodiscard]] static Error attachFrame(
                ErrorCode errorCode,
                std::string_view description,
                std::source_location location,
                std::string message,
                std::optional<Error> cause
            ) noexcept;

            ErrorCode m_code{};
            std::string_view m_description;
            std::source_location m_location;
            std::shared_ptr<const detail::Node> m_node;
            /// @endcond
    };
} // namespace viscera

#include "detail/Node.hpp"

namespace viscera
{
    inline Error::Error(const Error &) noexcept = default;
    inline Error::Error(Error &&) noexcept = default;
    inline Error &Error::operator=(const Error &) noexcept = default;
    inline Error &Error::operator=(Error &&) noexcept = default;
    inline Error::~Error() = default;

    /// @cond
    inline detail::Node::Node(std::string messageIn, std::optional<Error> causeIn) noexcept :
        message(std::move(messageIn)),
        cause(std::move(causeIn)) {}

    inline Error::Error(
        const ErrorCode errorCode,
        const std::string_view description,
        const std::source_location location,
        std::shared_ptr<const detail::Node> node
    ) noexcept :
        m_code(errorCode),
        m_description(description),
        m_location(location),
        m_node(std::move(node)) {}

    template <ErrorEnum E>
    ErrorCode Error::toCode(const E errorCode) noexcept
    {
        return ErrorCode{
            errorDomain(errorCode),
            static_cast<std::uint16_t>(static_cast<std::underlying_type_t<E>>(errorCode)),
        };
    }

    inline Error Error::makeFrame(
        const ErrorCode errorCode,
        const std::string_view description,
        const std::source_location location,
        std::string message,
        std::optional<Error> cause
    )
    {
        auto node = std::make_shared<const detail::Node>(std::move(message), std::move(cause));
        return Error{errorCode, description, location, std::move(node)};
    }

    inline Error Error::attachFrame(
        const ErrorCode errorCode,
        const std::string_view description,
        const std::source_location location,
        std::string message,
        std::optional<Error> cause
    ) noexcept
    {
        try
        {
            return makeFrame(errorCode, description, location, std::move(message), std::move(cause));
        }
        catch (...)
        {
            panic("error frame allocation failed", location);
        }
    }

    /// @endcond

    inline std::string_view Error::message() const noexcept
    {
        if (m_node == nullptr)
        {
            return {};
        }

        return m_node->message;
    }

    inline const Error *Error::cause() const noexcept
    {
        if (m_node == nullptr || !m_node->cause.has_value())
        {
            return nullptr;
        }

        return std::addressof(*m_node->cause);
    }

    template <ErrorEnum E>
    Error::Error(const E errorCode, const std::source_location where) noexcept :
        m_code(toCode(errorCode)),
        m_description(errorMessage(errorCode)),
        m_location(where),
        m_node(nullptr) {}

    template <ErrorEnum E>
    bool Error::is(const E expected) const noexcept
    {
        return is(toCode(expected));
    }

    namespace detail
    {
        /// @cond
        struct FormatAttempt
        {
            std::string text;
            bool failed{false};
        };

        template <typename ...Args>
        [[nodiscard]] FormatAttempt
            formatMessage(const FormatString<std::type_identity_t<Args> ...> &format, Args && ...args) noexcept
        {
            try
            {
                return FormatAttempt{
                    .text = fmt::format(format.format, std::forward<Args>(args) ...),
                    .failed = false,
                };
            }
            catch (const std::bad_alloc &)
            {
                panic("error frame allocation failed", format.location);
            }
            catch (...)
            {
                return FormatAttempt{.text = {}, .failed = true};
            }
        }

        /// @endcond
    } // namespace detail

    template <ErrorEnum E, typename ...Args>
    Error::Error(const E errorCode, FormatString<std::type_identity_t<Args> ...> format, Args && ...args) noexcept :
        m_code(toCode(errorCode)),
        m_description(errorMessage(errorCode)),
        m_location(format.location),
        m_node(nullptr)
    {
        auto attempt = detail::formatMessage(format, std::forward<Args>(args) ...);
        if (attempt.failed)
        {
            return;
        }

        *this = attachFrame(m_code, m_description, m_location, std::move(attempt.text), std::nullopt);
    }

    template <typename ...Args>
    Error Error::withContext(FormatString<std::type_identity_t<Args> ...> format, Args && ...args) const noexcept
    {
        auto attempt = detail::formatMessage(format, std::forward<Args>(args) ...);
        if (attempt.failed)
        {
            return *this;
        }

        return attachFrame(
            m_code,
            m_description,
            format.location,
            std::move(attempt.text),
            std::optional<Error>{*this}
        );
    }

    template <ErrorEnum E, typename ...Args>
    Error
        Error::wrap(
            const E newCode,
            FormatString<std::type_identity_t<Args> ...> format,
            Args && ...args
        ) const noexcept
    {
        auto attempt = detail::formatMessage(format, std::forward<Args>(args) ...);
        if (attempt.failed)
        {
            attempt.text.clear();
        }

        return attachFrame(
            toCode(newCode),
            errorMessage(newCode),
            format.location,
            std::move(attempt.text),
            std::optional<Error>{*this}
        );
    }

    static_assert(sizeof(Error) <= kMaxErrorBytes);
    static_assert(std::is_nothrow_copy_constructible_v<Error>);
    static_assert(std::is_nothrow_move_constructible_v<Error>);
    static_assert(std::is_nothrow_copy_assignable_v<Error>);
    static_assert(std::is_nothrow_move_assignable_v<Error>);
    static_assert(std::is_nothrow_destructible_v<Error>);
} // namespace viscera

namespace fmt
{
    /**
     * @brief Single-line formatter for viscera::Error.
     *
     * Writes through the output iterator. Does not mutate the error, build a
     * temporary std::string, or strip a source-root prefix. Exceptions from
     * {fmt} are caught and replaced with a short fallback. Prints at most
     * viscera::kMaxErrorFormatFrames frames, outermost first.
     *
     * @invariant Does not modify the error or its frames.
     * @see viscera::kMaxErrorFormatFrames
     */
    template <>
    struct formatter<viscera::Error>
    {
        /**
             * @brief Accepts @c {} and ignores a format spec.
             *
             * @param context Parse context positioned at the spec.
             * @return Iterator at the closing brace, or the end of the format string.
             */
        static constexpr auto parse(fmt::format_parse_context &context) -> fmt::format_parse_context::iterator
        {
            const auto *it = context.begin();
            const auto *const end = context.end();
            while (it != end && *it != '}')
            {
                ++it;
            }

            return it;
        }

        /**
             * @brief Writes the error line.
             *
             * @param error   Error to render.
             * @param context Format context whose output iterator receives the line.
             * @return The advanced output iterator.
             * @post @p error is unchanged.
             * @note Within the frame cap the parenthetical location is the root
             *       origin. Past the cap the line contains @c " ..." and the
             *       location is the last frame printed.
             */
        static auto format(const viscera::Error &error, const format_context &context) -> format_context::iterator;
    };
} // namespace fmt
