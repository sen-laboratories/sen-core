/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * The settings of the server for the settings window (kCoreSettings, kCoreSettingsSet): what it describes, what it applies and stores, and what it refuses.
 * Runs against the sen_server that is running (core-settings.sh starts the one under test) and puts the settings back as they were.
 */

#include <Application.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Messenger.h>
#include <Path.h>

#include <stdio.h>
#include <string.h>

#include <sen/Sen.h>
#include <sen/SenSettingsForm.h>

namespace form = sen::settings::form;

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static const char kKey[] = "SEN:modifierKey";
static const char kFileTypes[] = "SEN:useFileTypes";


static status_t
Ask(uint32 what, const BMessage* changes, BMessage* reply)
{
	BMessage request(what);
	if (changes != NULL)
		request.AddMessage(form::kFieldSettings, changes);
	return BMessenger(sen::kServerSignature).SendMessage(&request, reply, 5000000, 5000000);
}


/** the settings as the server has stored them (the file of the settings of SEN) */
static int32
StoredKey()
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("sen");
	path.Append(sen::config::kSettingsFile);
	BFile file(path.Path(), B_READ_ONLY);
	BMessage stored;
	if (file.InitCheck() != B_OK || stored.Unflatten(&file) != B_OK)
		return -1;
	return stored.GetInt32(sen::key::kModifierKey, -1);
}


int main()
{
	BApplication app("application/x-vnd.sen-labs.core-settings-test");

	BMessage reply;
	CHECK(Ask(sen::cmd::kCoreSettings, NULL, &reply) == B_OK);
	CHECK(reply.GetInt32(sen::key::kResult, -1) == B_OK);
	BMessage settings, schema, defaults;
	CHECK(reply.FindMessage(form::kFieldSettings, &settings) == B_OK);
	CHECK(reply.FindMessage(form::kFieldSchema, &schema) == B_OK);
	CHECK(reply.FindMessage(form::kFieldDefaults, &defaults) == B_OK);

	// what it describes: the key and the file types, a schema with the choices of the key, defaults
	int32 key = 0;
	bool fileTypes = false;
	CHECK(settings.FindInt32(kKey, &key) == B_OK);
	CHECK(settings.FindBool(kFileTypes, &fileTypes) == B_OK);
	BMessage keySchema;
	CHECK(schema.FindMessage(kKey, &keySchema) == B_OK);
	CHECK(keySchema.CountNames(B_STRING_TYPE) >= 3);
	type_code type;
	int32 choices = 0;
	CHECK(keySchema.GetInfo(form::kAttrChoices, &type, &choices) == B_OK && choices == 4);
	CHECK(keySchema.CountNames(B_ANY_TYPE) >= 4);
	CHECK(strcmp(keySchema.GetString(form::kAttrKind, ""), form::kKindChoice) == 0);
	CHECK(defaults.GetInt32(kKey, -1) == B_COMMAND_KEY);
	CHECK(defaults.GetBool(kFileTypes, false));
	const int32 originalKey = key;
	const bool originalFileTypes = fileTypes;

	// a change: applied, stored, and the answer has the settings as they are now
	const int32 other = originalKey == B_CONTROL_KEY ? B_SHIFT_KEY : B_CONTROL_KEY;
	BMessage change;
	change.AddInt32(kKey, other);
	reply.MakeEmpty();
	CHECK(Ask(sen::cmd::kCoreSettingsSet, &change, &reply) == B_OK);
	CHECK(reply.GetInt32(sen::key::kResult, -1) == B_OK);
	BMessage now;
	CHECK(reply.FindMessage(form::kFieldSettings, &now) == B_OK && now.GetInt32(kKey, -1) == other);
	snooze(300000);
	CHECK(StoredKey() == other);

	// values that are not valid are refused and told, the rest of the change is made, nothing else changes
	BMessage bad;
	bad.AddInt32(kKey, 999);					// no such key
	bad.AddString("nonsense", "x");				// no such setting
	bad.AddBool(kFileTypes, !originalFileTypes);
	reply.MakeEmpty();
	CHECK(Ask(sen::cmd::kCoreSettingsSet, &bad, &reply) == B_OK);
	CHECK(reply.GetInt32(sen::key::kResult, B_OK) != B_OK);
	const char* detail = reply.GetString(sen::key::kDetail, "");
	CHECK(strstr(detail, kKey) != NULL && strstr(detail, "nonsense") != NULL);
	CHECK(reply.FindMessage(form::kFieldSettings, &now) == B_OK);
	CHECK(now.GetInt32(kKey, -1) == other);									// (the refused value is not taken)
	CHECK(now.GetBool(kFileTypes, originalFileTypes) == !originalFileTypes);	// (the valid one is)

	// a value of a wrong type
	BMessage wrong;
	wrong.AddString(kKey, "many");
	reply.MakeEmpty();
	CHECK(Ask(sen::cmd::kCoreSettingsSet, &wrong, &reply) == B_OK);
	CHECK(reply.GetInt32(sen::key::kResult, B_OK) == B_BAD_VALUE);
	CHECK(reply.FindMessage(form::kFieldSettings, &now) == B_OK && now.GetInt32(kKey, -1) == other);

	// the whole message (what a revert or the defaults send), here the settings as they were
	BMessage whole;
	whole.AddInt32(kKey, originalKey);
	whole.AddBool(kFileTypes, originalFileTypes);
	reply.MakeEmpty();
	CHECK(Ask(sen::cmd::kCoreSettingsSet, &whole, &reply) == B_OK);
	CHECK(reply.GetInt32(sen::key::kResult, -1) == B_OK);
	CHECK(reply.FindMessage(form::kFieldSettings, &now) == B_OK);
	CHECK(now.GetInt32(kKey, -1) == originalKey && now.GetBool(kFileTypes, !originalFileTypes) == originalFileTypes);
	snooze(300000);
	CHECK(StoredKey() == originalKey);

	// no settings in the request
	reply.MakeEmpty();
	CHECK(Ask(sen::cmd::kCoreSettingsSet, NULL, &reply) == B_OK);
	CHECK(reply.GetInt32(sen::key::kResult, B_OK) != B_OK);

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
