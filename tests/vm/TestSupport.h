/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * Helpers for the tests that run against a live sen_server in a Haiku VM: files that can be related, sending messages to the
 * server, reading the attributes that it wrote.
 */

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <fs_attr.h>

#include <stdlib.h>

#include <string>
#include <vector>

#include <sen/Sen.h>

#include "../../src/relations/IdListAttr.h"

inline constexpr const char* kReference = sen::mime::kReferenceRelation;
inline constexpr const char* kAssociation = sen::mime::kAssociationRelation;
inline constexpr const char* kLabelType = "classification/x-vnd.sen-labs.entity.label";

// ---- helpers

inline BString
TestDir()
{
	const char* dir = getenv("TEST_DIR");
	return BString(dir != NULL ? dir : "/Develop/test") << "/_senrel";
}

/** A folder for one test with files that can be related. */
struct Fixture {
	BString		dir;
	std::vector<entry_ref> refs;

	Fixture(const char* name, int fileCount)
	{
		dir = TestDir();
		dir << "/" << name;
		BString command("rm -rf '");
		command << dir << "' && mkdir -p '" << dir << "'";
		system(command.String());

		for (int i = 0; i < fileCount; i++)
			refs.push_back(Create(BString("file") << i));
	}

	~Fixture()
	{
		BString command("rm -rf '");
		command << dir << "'";
		system(command.String());
	}

	entry_ref Create(const char* name, const char* mimeType = NULL)
	{
		BString path(dir);
		path << "/" << name;
		BFile file(path.String(), B_CREATE_FILE | B_READ_WRITE);
		if (mimeType != NULL) {
			BNodeInfo info(&file);
			info.SetType(mimeType);
		}
		BEntry entry(path.String());
		entry_ref ref;
		entry.GetRef(&ref);
		return ref;
	}
};

inline std::string
IdOf(const entry_ref& ref)
{
	BNode node(&ref);
	BString id;
	node.ReadAttrString(sen::attr::kId, &id);
	return id.String();
}

inline std::vector<std::string>
List(const entry_ref& ref, const char* base)
{
	BNode node(&ref);
	std::vector<std::string> ids;
	sen::ReadIdList(node, base, &ids);
	return ids;
}

/** the relations of a type stored with a file, empty if there are none */
inline BMessage
Stored(const entry_ref& ref, const char* type)
{
	BString attr(sen::attr::kRelationPrefix);
	attr << (type + strlen(sen::mime::kRelationPrefix));

	BMessage relations;
	BNode node(&ref);
	attr_info info;
	if (node.GetAttrInfo(attr.String(), &info) != B_OK)
		return relations;
	std::vector<char> buffer(info.size);
	node.ReadAttr(attr.String(), B_MESSAGE_TYPE, 0, buffer.data(), info.size);
	relations.Unflatten(buffer.data());
	return relations;
}

inline int32
Sets(const entry_ref& ref, const char* type, const std::string& targetId)
{
	BMessage relations = Stored(ref, type);
	type_code code;
	int32 count = 0;
	if (relations.GetInfo(targetId.c_str(), &code, &count) != B_OK)
		return 0;
	return count;
}

inline bool
HasAttr(const entry_ref& ref, const char* name)
{
	BNode node(&ref);
	attr_info info;
	return node.GetAttrInfo(name, &info) == B_OK;
}

inline BMessage
Send(uint32 what, const entry_ref* source, const char* type, const entry_ref* target = NULL,
	const BMessage* properties = NULL)
{
	BMessage message(what);
	if (source != NULL)
		message.AddRef(sen::key::kSourceRef, source);
	if (type != NULL)
		message.AddString(sen::key::kRelationType, type);
	if (target != NULL)
		message.AddRef(sen::key::kTargetRef, target);
	if (properties != NULL)
		message.AddMessage(sen::key::kRelationProperties, properties);

	BMessage reply;
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid() || server.SendMessage(&message, &reply, 10000000, 10000000) != B_OK)
		reply.AddInt32(sen::key::kStatus, -1);
	return reply;
}

inline int32
SenStatus(const BMessage& reply)
{
	return reply.GetInt32(sen::key::kStatus, -1);
}

inline BMessage
Page(int32 page)
{
	BMessage properties;
	properties.AddInt32("schema:pageStart", page);
	return properties;
}

