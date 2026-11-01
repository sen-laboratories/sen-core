/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file Cli.h
 * @brief The command line of the SEN server: the options that print information about SEN and exit, to help find problems.
 *
 *   --version   the name and version of the server (from its application resource), with the date and build number
 *   --info      an overview: the number of SEN objects (files with a SEN:ID) on all volumes, the types and relations installed
 *   --types     the installed meta types, entities and relations as a tree, with the number of files of each type
 *   --check     look for inconsistencies: duplicate SEN:IDs, files whose type is not installed (a broken ontology), missing indices
 *   --migrate   the steps that make the data of the user what this version expects (see migrations/Migrations.h); --dry-run only counts
 *   --help      the options
 *
 * None of them needs the server to run, or starts it (--migrate only runs the steps while it is not running).
 */

/**
 * @return the exit code of the program if an option was handled (or wrong), -1 if the server is to be started
 */
int RunCommandLine(int argc, char* argv[]);
