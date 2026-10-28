/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Window.h>

#include <map>
#include <string>

#include <sen/SenGraph.h>

class BButton;
class BCheckBox;
class BFilePanel;
class BStringView;
class SenConfigHandler;

namespace sen {
namespace ui {

class GraphView;

/** to the window: centre the graph on a file (refs) */
static const uint32 kGraphFocus = 'GWfo';

/**
 * The window of the Graph item of the Deskbar menu (also the Knot with the SEN key): the objects of SEN as the nodes of a graph and their relations as its
 * edges, laid out by SenGraph.h. The objects are read in a thread of their own (see graph::Collect()). A click selects an object (its relations are
 * emphasised), a double click opens it as Tracker does; the mouse wheel zooms, dragging moves, F fits the graph into the window. The graph can be
 * saved as SVG.
 */
class GraphWindow : public BWindow {
public:
					GraphWindow(SenConfigHandler* config);
	virtual			~GraphWindow();

	virtual void	DispatchMessage(BMessage* message, BHandler* handler);
	virtual void	MessageReceived(BMessage* message);

private:
	void			_Start(bool collect);
	void			_Done(BMessage* message);
	void			_Open(const BMessage* message);
	void			_ApplyFocus();
	void			_OpenRelations(const BMessage* message);
	void			_Save(const BMessage* message);
	static int32	_WorkEntry(void* data);

	GraphView*		fView;
	BCheckBox*		fMeta;
	BCheckBox*		fUnconnected;
	BCheckBox*		fDark;
	BButton*		fSaveSelection;
	SenConfigHandler*	fConfig;
	bool			fSelectionOnly;	///< what the file panel is for: the selection, not the view
	BButton*		fRefresh;
	BStringView*	fStatus;
	BFilePanel*		fSavePanel;

	bool							fWorking;
	sen::graph::Graph				fAll;		///< every object and relation that was read
	std::map<std::string, entry_ref>	fRefs;
	bool							fTruncated;
	int32							fObjects;
	bool							fHasFocus;	///< a file to centre the graph on, as soon as the graph is there
	entry_ref						fFocusRef;
};

}	// namespace ui
}	// namespace sen
