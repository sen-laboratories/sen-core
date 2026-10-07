/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <String.h>

#include <string.h>

#include <sen/Sen.h>

/**
 * @file Reply.h
 * @brief The envelope of every reply of the SEN server: `status`, `result`, `detail`, `apiVersion` (see sen::key).
 */
namespace sen {
namespace reply {

/**
 * @brief The SEN status code that tells a Haiku error on the domain level, if the handler did not set a more specific one.
 * @param result the `status_t` of the operation
 */
inline int32
StatusFor(status_t result)
{
    switch (result) {
        case B_OK:                  return status::kOk;
        case B_ENTRY_NOT_FOUND:
        case B_NAME_NOT_FOUND:      return status::kErrNotFound;
        case B_BAD_VALUE:
        case B_BAD_TYPE:
        case B_NOT_SUPPORTED:
        case B_MISMATCHED_VALUES:   return status::kErrBadRequest;
        case B_NAME_IN_USE:
        case B_DUPLICATE_REPLY:
        case B_FILE_EXISTS:         return status::kErrConflict;
        case B_BUFFER_OVERFLOW:     return status::kErrTooManyTargets;
        case B_BUSY:
        case B_TIMED_OUT:           return status::kErrUnavailable;
        default:                    return status::kErrFailed;
    }
}

/** @brief Set the SEN status code of a reply, replacing a code that is there already. */
inline void
SetStatus(BMessage* reply, int32 code)
{
    if (reply->HasInt32(key::kStatus))
        reply->ReplaceInt32(key::kStatus, code);
    else
        reply->AddInt32(key::kStatus, code);
}

/** @brief Set the text that says what happened, replacing a text that is there already. */
inline void
SetDetail(BMessage* reply, const char* detail)
{
    if (reply->HasString(key::kDetail))
        reply->ReplaceString(key::kDetail, detail);
    else
        reply->AddString(key::kDetail, detail);
}

/**
 * @brief Complete a reply with the fields that every reply has.
 *
 * `status` is the SEN code (the one set by the handler with SetStatus(), else derived from the result), `result` the
 * technical `status_t`, `detail` the text (the handler's, else the text of the error), `apiVersion` the version of the API.
 *
 * @param reply  the reply to complete
 * @param result the `status_t` of the operation
 */
inline void
Finish(BMessage* reply, status_t result)
{
    if (! reply->HasInt32(key::kStatus))
        reply->AddInt32(key::kStatus, StatusFor(result));

    if (reply->HasInt32(key::kResult))
        reply->ReplaceInt32(key::kResult, result);
    else
        reply->AddInt32(key::kResult, result);

    if (! reply->HasString(key::kDetail))
        reply->AddString(key::kDetail, strerror(result));

    if (! reply->HasInt32(key::kApiVersion))
        reply->AddInt32(key::kApiVersion, kApiVersion);
}

}   // namespace reply
}   // namespace sen
