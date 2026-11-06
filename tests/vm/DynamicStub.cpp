/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * A navigator plugin for DynamicRelationTest.cpp: answers sensei::cmd::kResolve (see Sensei.h) as the spatial navigator does, with fixed content, so that the hook of the server can be
 * tested without the network and without places. For the source file it finds the files of its folder that are called `target*` (sorted by name) and answers each as a target; the first,
 * the third... have the label `within`, the second, the fourth... the label `contains`. Each target has the property `SEN:REL:Note` that says what the request held: `libraries=<n>` is the
 * number of Library folders that the server gave (sensei::key::kLibraries), `folder=<name>` the name of the folder of the first, so the test sees that they reach the plugin.
 */
#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

#include <algorithm>
#include <string>
#include <vector>

#include <sen/Sen.h>
#include <sen/Sensei.h>

class StubApp : public BApplication
{
public:
	StubApp() : BApplication("application/x-vnd.sen-plugin.sen-labs.test-dynamic-stub") {}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != sensei::cmd::kResolve) {
			BApplication::MessageReceived(message);
			return;
		}
		BMessage reply(sensei::cmd::kResult);
		entry_ref source;
		status_t result = message->FindRef("refs", &source);
		if (result == B_OK) {
			BMessage libraries;
			message->FindMessage(sensei::key::kLibraries, &libraries);
			int32 count = libraries.CountNames(B_REF_TYPE);
			std::string note = "libraries=" + std::to_string(count);
			char* name;
			type_code code;
			entry_ref folder;
			if (libraries.GetInfo(B_REF_TYPE, 0, &name, &code) == B_OK && libraries.FindRef(name, &folder) == B_OK)
				note += " folder=" + std::string(BPath(&folder).Leaf());

			BDirectory directory;
			BEntry sourceEntry(&source), parent;
			if (sourceEntry.GetParent(&parent) == B_OK)
				directory.SetTo(&parent);
			std::vector<entry_ref> targets;
			entry_ref each;
			while (directory.GetNextRef(&each) == B_OK) {
				if (std::string(each.name).compare(0, 6, "target") == 0)
					targets.push_back(each);
			}
			std::sort(targets.begin(), targets.end(), [](const entry_ref& a, const entry_ref& b) { return std::string(a.name) < std::string(b.name); });
			for (size_t i = 0; i < targets.size(); i++) {
				BMessage item;
				item.AddRef(sen::key::kTargetRef, &targets[i]);
				item.AddString(sen::attr::kRelationLabel, i % 2 == 0 ? "within" : "contains");
				item.AddString(sen::attr::kRelationKind, "STUB");
				item.AddString("SEN:REL:Note", note.c_str());
				reply.AddMessage(sensei::key::kTargets, &item);
			}
			// a ref that does not exist is not a target: the server leaves it out
			if (!targets.empty()) {
				BMessage ghost;
				entry_ref gone(targets[0].device, targets[0].directory, "target-that-is-not-there");
				ghost.AddRef(sen::key::kTargetRef, &gone);
				ghost.AddString(sen::attr::kRelationLabel, "within");
				reply.AddMessage(sensei::key::kTargets, &ghost);
			}
		}
		reply.AddInt32(sensei::key::kResult, result);
		message->SendReply(&reply, this);
		Quit();
	}
};

int
main()
{
	StubApp app;
	app.Run();
	return 0;
}
