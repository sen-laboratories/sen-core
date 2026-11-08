/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "PluginChannels.h"

#include <Autolock.h>
#include <Locker.h>

#include <map>
#include <string>

#include <spdlog/spdlog.h>

#include "../ui/Worker.h"

namespace sen {
namespace plugin {

namespace {

BLocker sLock("sen plugin channels");
std::map<std::string, sen::ui::Worker*> sChannels;

}	// namespace


status_t
Run(const char* signature, const std::function<void()>& work)
{
	if (signature == NULL || signature[0] == '\0')
		return B_BAD_VALUE;

	sen::ui::Worker* channel = NULL;
	{
		BAutolock lock(sLock);
		auto found = sChannels.find(signature);
		if (found == sChannels.end()) {
			std::string name = std::string("sen plugin ") + signature;
			found = sChannels.emplace(signature, new sen::ui::Worker(name.c_str(), B_NORMAL_PRIORITY)).first;
			spdlog::debug("plugin channel for {} made", signature);
		}
		channel = found->second;
	}
	return channel->Post([work]() -> BMessage* {
		work();
		return NULL;
	}, BMessenger());
}


void
Finish()
{
	BAutolock lock(sLock);
	for (auto& channel : sChannels)
		channel.second->Finish();
	sChannels.clear();
}

}	// namespace plugin
}	// namespace sen
