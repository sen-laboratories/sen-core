/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <SupportDefs.h>

#include <functional>

namespace sen {
namespace plugin {

/**
 * The communication of the server with a plugin has a looper (and so a thread and a message queue) of its own for each plugin. A plugin may take its time (a web service that
 * works out its answer for half a minute, a dialog for the user), and the server must not wait for it in the thread that serves everybody: so a request that needs a plugin is taken
 * over by the channel of that plugin and answered from there, and the thread of the server goes on with the next message, among them those that the plugin itself sends (it makes
 * a file, and asks the server to put it into the current context). Requests for one plugin are handled one after the other, in the order in which they came; those for different
 * plugins at the same time.
 *
 * Do not call these from the thread of a channel for the same plugin and wait for the result: the channel would wait for itself.
 */

/**
 * Has @p work done by the channel of the plugin with this signature, which is made when it is first needed. @p work runs in the thread of the channel.
 * @return B_OK if the work is queued
 */
status_t Run(const char* signature, const std::function<void()>& work);

/** Tells every channel to end when the work that is queued is done (the server quits). */
void Finish();

}	// namespace plugin
}	// namespace sen
