/*
 * Copyright 2023, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#pragma once

#include <Application.h>

class SenConfigHandler : public BHandler {

public:
                SenConfigHandler();
    status_t    Init();
    status_t    GetConfig(BMessage* settingsMsg);

virtual         ~SenConfigHandler();

virtual	void	MessageReceived(BMessage* message);

private:
    status_t    InitDefaultSettings(BPath* path, BMessage* message);

    status_t    LoadSettings(BMessage* message);

    status_t    SaveSettings(const BMessage* message);

    /**
     * creates a new context with the given name, returns the `entry_ref` of the new file.
     * Fails if context directory already exists.
     *
     * @param name  the custom name of the context
     * @param ref   an optional `entry_ref` to hold the resulting context dir ref
     *
     * @return `B_OK` or the status code of the last error.
     */
    status_t    CreateContext(const char* name, entry_ref* ref = NULL);

    /**
     * shortcut to quickly resolve the corresponding directory for the given context.
     *
     * @param context  the custom name of the context
     * @param ref      an empty `entry_ref` to hold the resulting context dir ref
     * @return `B_OK` or the status code of the last error.
     */
    status_t    GetContextDir(const char* context, entry_ref* ref);

    /**
     * shortcut to quickly resolve the corresponding base directory for the given classification type in a context.
     *
     * @param context  the custom context name
     * @param type     the custom type name (MIME type)
     * @param ref      an empty `entry_ref` to hold the resulting context dir ref
     * @param create   create directory if it does not exist?
     * @return B_OK or the status code of the last error.
     */
    status_t    GetClassificationDir(const char* context, const char* type, entry_ref* ref, bool create = false);

    /**
     * finds the corresponding context directory for a given context name and returns the
     * `entry_ref` and configuration.
     *
     * reply message format is as following:
     *
     * Field Name    | Type         | Description
     * ------------- | ------------ | --------
     * refs          | `B_REF_TYPE` | collection of the context's entry_refs
     * config        | `BMessage`   | the context's config message
     *
     * @param name      the custom name of the context
     * @param reply     an empty message to hold the reply
     * @return B_OK or the status code of the last error.
     */
    status_t    FindContextByName(const char* name, BMessage *reply);

    /**
     * adds a classification entity under a given context with custom name and MIME type.
     *
     * @param context   optional, pass empty string for default context
     * @param name      a custom name.
     * @param type      the MIME type of the META entity to use for the context
     * @param reply     an empty message to hold the result, which is the entry_ref
     *                  of the newly created entity in the message field "refs"
     * @return the status code of the first error or B_OK
     */
    status_t    AddClassification(const char* context, const char* name, const char* type, BMessage* reply);

    /**
     * get a classification entity for a given context with custom name and MIME type.
     *
     * There can only be one classification entity for a given context, type and name.
     * Use a different context if you encounter this intentional limit.
     *
     * @param context   optional, pass empty string for default context
     * @param name      the entity name of the classification.
     * @param type      the MIME type of the META entity of the context
     * @param reply     an empty BMessage to hold the result
     */
    status_t    GetClassification(const char* context, const char* name, const char* type, BMessage* reply);

    BDirectory* fSettingsDir;
    BMessage*   fSettingsMsg;
};
