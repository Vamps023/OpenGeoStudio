#pragma once

// ═══════════════════════════════════════════════════════════
// RoadError — Structured Error Type for RoadEngine
// ═══════════════════════════════════════════════════════════
//
// @file road_engine/public/road_error.hpp
// @brief Structured error type with code, message, and optional context
//
// Error codes are organized by category:
//   1000-1999: Geometry errors (invalid parameters, failed computations)
//   2000-2999: Parsing errors (malformed input, schema violations)
//   3000-3999: Serialization errors (encoding failures, schema violations)
//   4000-4999: Mesh generation errors (invalid parameters, triangulation failures)
//   5000-5999: I/O errors (file access, format errors)
//
// @section Usage
// Functions that can fail return Result<T, RoadError> (or std::expected<T, RoadError>
// when C++23 is available). Callers must check for errors before using the result.
//
// @section Logging
// For functions that cannot return errors (constructors, destructors, callbacks),
// errors are logged via the logging callback mechanism (see road_log.hpp).

#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

namespace road_engine {

// ═══════════════════════════════════════════════════════════
// Error Code Constants
// ═══════════════════════════════════════════════════════════

/// Geometry error codes (1000-1999)
enum class GeometryError : int32_t {
    InvalidParameter    = 1001,  ///< Parameter value is invalid (e.g., zero radius)
    ArithmeticOverflow  = 1002,  ///< Computation overflow (e.g., segment length > 1e6)
    DegenerateGeometry  = 1003,  ///< Geometry is degenerate (e.g., zero-length segment)
    ConvergenceFailure  = 1004,  ///< Iterative algorithm failed to converge
    OutOfRange          = 1005,  ///< Parameter value is out of valid range
};

/// Parsing error codes (2000-2999)
enum class ParseError : int32_t {
    MalformedJson       = 2001,  ///< JSON syntax error
    SchemaViolation     = 2002,  ///< JSON valid but doesn't match road schema
    MissingField        = 2003,  ///< Required field is missing
    InvalidType         = 2004,  ///< Field has wrong type
    UnknownSegmentKind  = 2005,  ///< SegmentKind value is not recognized
};

/// Serialization error codes (3000-3999)
enum class SerializeError : int32_t {
    EncodingFailure     = 3001,  ///< Failed to encode to target format
    SchemaOutputError   = 3002,  ///< Output doesn't conform to schema
    FieldConversionError= 3003,  ///< Could not convert field for output
};

/// Mesh generation error codes (4000-4999)
enum class MeshError : int32_t {
    TriangulationFailure= 4001,  ///< Ear-clipping or CDT triangulation failed
    InvalidPolygon      = 4002,  ///< Input polygon is self-intersecting or degenerate
    NoVertices          = 4003,  ///< Mesh has no vertices
    IndexOutOfRange     = 4004,  ///< Triangle index references non-existent vertex
};

/// I/O error codes (5000-5999)
enum class IoError : int32_t {
    FileNotFound        = 5001,  ///< File does not exist
    PermissionDenied    = 5002,  ///< File access permission denied
    FormatError         = 5003,  ///< File format is not recognized
};

// ═══════════════════════════════════════════════════════════
// Context Entry — Key-value pair for additional error context
// ═══════════════════════════════════════════════════════════

/// @brief Key-value context entry for additional error information
struct ErrorContextEntry {
    std::string key;    ///< Context key (e.g., "segmentIndex", "parameterName")
    std::string value;  ///< Context value (e.g., "5", "radius")
};

// ═══════════════════════════════════════════════════════════
// RoadError — Structured Error Type
// ═══════════════════════════════════════════════════════════

/// @brief Structured error with code, message, and optional context
///
/// RoadError is the primary error type returned by RoadEngine functions.
/// It contains:
/// - An error code (int32_t, range 1000-9999) identifying the specific error
/// - A human-readable message (max 512 characters)
/// - Optional context as key-value string pairs (max 8 entries, 256 chars per value)
///
/// @ingroup ErrorHandling
struct RoadError {
    /// Error code in range 1000-9999 (see GeometryError, ParseError, etc.)
    int32_t code = 0;

    /// Human-readable error message (max 512 characters)
    std::string message;

    /// Optional key-value context entries (max 8 entries)
    std::vector<ErrorContextEntry> context;

    // ─── Constructors ──────────────────────────────────────

    /// Default constructor — creates an empty (success) error
    RoadError() = default;

    /// Construct with code and message
    /// @param errCode Error code (1000-9999)
    /// @param errMsg Error message (max 512 chars, will be truncated)
    RoadError(int32_t errCode, std::string_view errMsg)
        : code(errCode)
        , message(errMsg.substr(0, 512)) {}

    /// Construct with code, message, and single context entry
    RoadError(int32_t errCode, std::string_view errMsg,
              std::string_view ctxKey, std::string_view ctxVal)
        : code(errCode)
        , message(errMsg.substr(0, 512)) {
        context.push_back({std::string(ctxKey), std::string(ctxVal).substr(0, 256)});
    }

    // ─── Context Management ────────────────────────────────

    /// Add a context entry (max 8 entries, later entries are ignored)
    /// @param key Context key
    /// @param value Context value (max 256 chars, will be truncated)
    void addContext(std::string_view key, std::string_view value) {
        if (context.size() >= 8) return;
        context.push_back({std::string(key), std::string(value).substr(0, 256)});
    }

    // ─── Query Methods ─────────────────────────────────────

    /// Returns true if this represents an error (code != 0)
    [[nodiscard]] bool isError() const { return code != 0; }

    /// Returns true if this represents success (code == 0)
    [[nodiscard]] bool ok() const { return code == 0; }

    /// Get error category as string (e.g., "Geometry", "Parse")
    [[nodiscard]] std::string_view category() const {
        if (code >= 1000 && code < 2000) return "Geometry";
        if (code >= 2000 && code < 3000) return "Parse";
        if (code >= 3000 && code < 4000) return "Serialize";
        if (code >= 4000 && code < 5000) return "Mesh";
        if (code >= 5000 && code < 6000) return "Io";
        return "Unknown";
    }

    /// Format error as string for logging
    [[nodiscard]] std::string format() const {
        std::string result = "[" + std::to_string(code) + "] " + std::string(category()) + ": " + message;
        for (const auto& ctx : context) {
            result += "\n  " + ctx.key + "=" + ctx.value;
        }
        return result;
    }
};

// ═══════════════════════════════════════════════════════════
// Result Type — Simple Result<T, E> for C++20
// ═══════════════════════════════════════════════════════════

/// @brief Result type that holds either a value or an error
///
/// This is a simple alternative to std::expected (C++23) for C++20 compatibility.
/// Use isOk() to check for success, value() to get the value, and error() to get the error.
///
/// @tparam T Value type
/// @ingroup ErrorHandling
template <typename T>
class Result {
public:
    /// Construct a success result
    static Result success(T val) {
        Result r;
        r.is_ok_ = true;
        r.value_ = std::move(val);
        return r;
    }

    /// Construct an error result
    static Result error(RoadError err) {
        Result r;
        r.is_ok_ = false;
        r.error_ = std::move(err);
        return r;
    }

    /// Returns true if the result contains a value
    [[nodiscard]] bool isOk() const { return is_ok_; }

    /// Returns true if the result contains an error
    [[nodiscard]] bool isError() const { return !is_ok_; }

    /// Get the value (undefined behavior if isError())
    [[nodiscard]] const T& value() const { return value_; }
    [[nodiscard]] T& value() { return value_; }

    /// Get the error (undefined behavior if isOk())
    [[nodiscard]] const RoadError& error() const { return error_; }

private:
    bool is_ok_ = false;
    T value_{};
    RoadError error_{};
};

/// Convenience function to create a success result
template <typename T>
Result<T> makeSuccess(T value) {
    return Result<T>::success(std::move(value));
}

/// Convenience function to create an error result
template <typename T>
Result<T> makeError(int32_t code, std::string_view message) {
    return Result<T>::error(RoadError(code, message));
}

} // namespace road_engine
