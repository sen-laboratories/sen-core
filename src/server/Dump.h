/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#pragma once

#include <Message.h>

#include <spdlog/spdlog.h>

namespace sen {

/**
 * Print a message to the log, but only when the log level is debug: messages (the config of every relation type, the replies) are large and
 * printing them on every request costs more than the request itself. The level is set with SEN_LOG_LEVEL.
 */
inline void
DumpMessage(const BMessage& message)
{
	if (spdlog::should_log(spdlog::level::debug))
		message.PrintToStream();
}

inline void
DumpMessage(const BMessage* message)
{
	if (message != NULL)
		DumpMessage(*message);
}

}	// namespace sen
