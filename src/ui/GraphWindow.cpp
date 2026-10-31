/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "GraphWindow.h"

#include "../config/SenConfigHandler.h"
#include "../graph/GraphData.h"

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <View.h>

#include <math.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include <sen/Sen.h>
#include <sen/SenEntityCard.h>
#include <sen/SenOntoCore.h>

#include <Bitmap.h>
#include <MimeType.h>
#include <functional>

namespace sen {
namespace ui {

using sen::graph::Edge;
using sen::graph::Node;

static const uint32 kRefresh = 'GWrf';
static const uint32 kFilter = 'GWfl';
static const uint32 kSave = 'GWsv';
static const uint32 kSaveSelection = 'GWss';
static const uint32 kSelectionChanged = 'GWse';
static const uint32 kPreviewToggled = 'GWpv';
#ifdef SEN_TESTING
static const uint32 kTestMouse = 'GWtm';	///< the mouse of a test: "screen_where", "down" (compiled only into a test build)
#endif
static const uint32 kDark = 'GWdk';
static const uint32 kWorkDone = 'GWdn';
static const uint32 kOpenNode = 'GWop';
static const uint32 kOpenRelations = 'GWor';
static const uint32 kSelectNode = 'GWsl';
static const uint32 kActivate = 'GWac';

/** the most objects that are shown (the layout and the drawing are made for some thousands, not more) */
static const size_t kMaxObjects = 2000;

static const char* const kTrackerSignature = "application/x-vnd.Be-TRAK";


static rgb_color
Color(const sen::graph::Rgb& color, uint8 alpha = 255)
{
	rgb_color result = {color.r, color.g, color.b, alpha};
	return result;
}


/** whether the colours of the system are dark (the background of documents) */
static bool
SystemIsDark()
{
	const rgb_color background = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	return (background.red * 299 + background.green * 587 + background.blue * 114) / 1000 < 128;
}


static BString
EdgeLabel(const Edge& edge)
{
	BString label(edge.label.c_str());
	if (edge.reciprocal && !edge.inverseLabel.empty() && edge.inverseLabel != edge.label) {
		if (!label.IsEmpty())
			label << " / ";
		label << edge.inverseLabel.c_str();
	}
	return label;
}


/** the distance of a point from the line between two points (not beyond its ends) */
static float
DistanceToSegment(BPoint point, BPoint a, BPoint b)
{
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float length2 = dx * dx + dy * dy;
	if (length2 < 0.0001f)
		return hypotf(point.x - a.x, point.y - a.y);
	float t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / length2;
	t = std::min(std::max(t, 0.0f), 1.0f);
	return hypotf(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}


/** the name of a relation type for people, as far as it is in the name: "relation/x-vnd.sen-labs.relation.person.friend" is "person.friend" */
static BString
ShortTypeName(const std::string& type)
{
	BString name(type.c_str());
	name.RemoveFirst("relation/");
	name.RemoveFirst("x-vnd.sen-labs.relation.");
	return name;
}


// ---- the view

/** The graph, drawn: moved by dragging, zoomed with the wheel, a click selects a node, a double click asks to open it. */
class GraphView : public BView {
public:
	GraphView()
		:
		BView("graph", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_NAVIGABLE),
		fScale(1),
		fOffset(0, 0),
		fNeedFit(true),
		fAutoFit(true),
		fSize(0, 0),
		fHover(-1),
		fSelected(-1),
		fCard(NULL),
		fCardKind(kNoCard),
		fCardIndex(-1),
		fCardNearFrom(true),
		fCardDistance(0),
		fCardAlign(sen::EntityCard::kCentred),
		fCardHot(false),
		fOverlays(true),
		fHoverEdge(-1),
		fSelectedEdge(-1),
		fDragging(false),
		fDark(SystemIsDark())
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		SetExplicitMinSize(BSize(400, 300));
	}

	void SetGraph(const sen::graph::Graph& graph)
	{
		fGraph = graph;
		_ClearCard();
		fHover = fSelected = fHoverEdge = fSelectedEdge = -1;
		fNeighbors.assign(fGraph.Nodes().size(), false);
		fNeedFit = true;
		fAutoFit = true;
		_SelectionChanged();
		Invalidate();
	}

	void SetMessage(const char* text)
	{
		fMessage = text;
		Invalidate();
	}

	const sen::graph::Graph& Graph() const { return fGraph; }

	bool Dark() const { return fDark; }

	bool HasSelection() const { return fSelected >= 0 || fSelectedEdge >= 0; }
	virtual ~GraphView() { delete fCard; }

#ifdef SEN_TESTING
	/** a move of the mouse, or a click, from a test (the real ones come from the app_server with the view that is under the mouse) */
	void TestMouse(BPoint where, bool down)
	{
		if (down)
			MouseDown(where);
		else
			MouseMoved(where, B_INSIDE_VIEW, NULL);
	}
#endif

	/** whether what is under the mouse gets a card (its attributes, or the relation) next to it */
	void SetOverlays(bool on)
	{
		fOverlays = on;
		if (!on)
			_ClearCard();
	}

	/** how the card of an object is made (the window knows the files of the nodes): by the id of the node, NULL if there is no file */
	void SetCardLoader(std::function<sen::EntityCard*(const std::string&)> loader) { fLoadCard = loader; }

	/** a name for the file of the selection: the node, or the nodes of the line */
	BString SelectionName() const
	{
		BString name;
		if (fSelected >= 0)
			name = fGraph.Nodes()[fSelected].label.c_str();
		else if (fSelectedEdge >= 0) {
			const Edge& edge = fGraph.Edges()[fSelectedEdge];
			name << fGraph.Nodes()[edge.from].label.c_str() << "-" << fGraph.Nodes()[edge.to].label.c_str();
		}
		// A name that needs no escaping anywhere: a viewer that makes a page with the file in it by its URL (a browser does that for an image) has trouble
		// with a space, and an escaped name (%20) is the name of another file for one that does not decode it. So everything that means something in a path
		// or a URL is a hyphen (letters of any language stay).
		const char* special = " /\\:*?\"<>|#%&;=+,'";
		for (const char* c = special; *c != '\0'; c++)
			name.ReplaceAll(BString().Append(*c, 1).String(), "-");
		return name;
	}

	/**
	 * The graph as SVG. The view as it is seen (its size, zoom and place, what is selected and dimmed, the names that are there), or, if only the
	 * selection is wanted, the selected node with the nodes it is connected to and its own lines (a selected line: its two nodes), as they are placed here
	 * and fitted into the picture. Empty if there is nothing to save.
	 */
	std::string Svg(bool selectionOnly) const
	{
		sen::graph::SvgOptions options;
		options.dark = fDark;
		options.fontSize = be_plain_font->Size();
		if (selectionOnly) {
			options.emphasiseLines = false;	// (all its lines are those of the node: they are plain, the node stands out)
			options.fontSize = 10;	// (a part is a picture of its own, fitted at the size that its nodes have: not the font of the system, which is for the view)
			if (fSelected >= 0) {
				sen::graph::Graph part = fGraph.Neighborhood(fSelected);
				options.selectedNode = part.IndexOf(fGraph.Nodes()[fSelected].id);
				options.edgeLabels = part.Edges().size() <= 8;	// (as in the view: the labels of many lines lie on each other, the names of the nodes tell more)
				return part.ToSvg(options);
			}
			if (fSelectedEdge >= 0) {
				sen::graph::Graph part = fGraph.Subgraph(std::vector<int>(), std::vector<int>(1, fSelectedEdge));
				options.selectedEdge = 0;
				return part.ToSvg(options);
			}
			return std::string();
		}
		if (fGraph.Nodes().empty())
			return std::string();
		options.width = Bounds().Width();
		options.height = Bounds().Height();
		options.scale = fScale;
		options.offsetX = fOffset.x;
		options.offsetY = fOffset.y;
		options.selectedNode = fSelected;
		options.selectedEdge = fSelectedEdge;
		if (fNodeLabelShown.size() == fGraph.Nodes().size() && fEdgeLabelShown.size() == fGraph.Edges().size()) {
			options.nodeLabelShown = &fNodeLabelShown;
			options.edgeLabelShown = &fEdgeLabelShown;
		}
		return fGraph.ToSvg(options);
	}

	/** Select the node of an id and put it in the middle of the view, zoomed in a little. */
	void Focus(const char* id)
	{
		const int32 node = fGraph.IndexOf(id);
		if (node < 0)
			return;
		_Select(node);
		fScale = std::max(fScale, 1.4f);
		fAutoFit = false;
		fNeedFit = false;
		const BRect bounds(Bounds());
		fOffset.x = bounds.Width() / 2 - fGraph.Nodes()[node].x * fScale;
		fOffset.y = bounds.Height() / 2 - fGraph.Nodes()[node].y * fScale;
		Invalidate();
	}

	/** select the node of an id (what a click does) */
	void SelectId(const char* id)
	{
		_Select(fGraph.IndexOf(id));
	}

	void SetDark(bool dark)
	{
		fDark = dark;
		Invalidate();
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		SetFlags(Flags() | B_SUBPIXEL_PRECISE);
		MakeFocus(true);
		fSize = BSize(Bounds().Width(), Bounds().Height());
	}

	virtual void Draw(BRect updateRect);

	virtual void MouseDown(BPoint where)
	{
		MakeFocus(true);
		BMessage* current = Window()->CurrentMessage();
		uint32 buttons = current->GetInt32("buttons", 0);
		if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
			return;

		// a click on the card does what a double click on the object does: it opens the file, or the relations of its type
		if (fCard != NULL && _CardFrame().Contains(where)) {
			_OpenCard();
			return;
		}

		const bool doubleClick = current->GetInt32("clicks", 1) >= 2;
		int32 node = _NodeAt(where);
		bool nearFrom = true;
		int32 edge = node < 0 ? _EdgeAt(where, &nearFrom) : -1;
		if (node >= 0) {
			_Select(node);
			if (doubleClick) {
				BMessage open(kOpenNode);
				open.AddString("id", fGraph.Nodes()[node].id.c_str());
				Window()->PostMessage(&open);
			}
		} else if (edge >= 0) {
			_SelectEdge(edge);
			if (doubleClick) {
				// a relation is in both nodes (reciprocal): the relations of its type of the node that is nearer to the click
				const Edge& selected = fGraph.Edges()[edge];
				BMessage open(kOpenRelations);
				open.AddString("id", fGraph.Nodes()[nearFrom ? selected.from : selected.to].id.c_str());
				open.AddString("type", selected.type.c_str());
				Window()->PostMessage(&open);
			}
		} else
			_Select(-1);
		fDragging = true;
		fLast = where;
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	}

	virtual void MouseUp(BPoint)
	{
		fDragging = false;
	}

	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage*)
	{
		if (transit == B_ENTERED_VIEW && !IsFocus())
			MakeFocus(true);	// (the keys, +, - and F, are for the graph when the mouse is over it)
		if (fDragging) {
			fAutoFit = false;
			fOffset.x += where.x - fLast.x;
			fOffset.y += where.y - fLast.y;
			fLast = where;
			_ClearCard();
			Invalidate();
			return;
		}
		if (transit == B_EXITED_VIEW) {
			fHover = fHoverEdge = -1;
			_ClearCard();
			Invalidate();
			return;
		}

		// the card stays while the mouse is on its way to it (the gap between the object and the card) and on it, where it lights up
		if (fCard != NULL && _InCardZone(where)) {
			const bool hot = _CardFrame().Contains(where);
			if (hot != fCardHot) {
				fCardHot = hot;
				Invalidate();
			}
			return;
		}

		int32 node = _NodeAt(where);
		bool nearFrom = true;
		int32 edge = node < 0 ? _EdgeAt(where, &nearFrom) : -1;
		if (node != fHover || edge != fHoverEdge || node >= 0 || edge >= 0) {
			fHover = node;
			fHoverEdge = edge;
			fMouse = where;
			if (!fOverlays) {
				Invalidate();
				return;
			}
			if (node >= 0) {
				if (fCardKind != kNodeCard || fCardIndex != node)
					_ShowNodeCard(node);
			} else if (edge >= 0) {
				_ShowEdgeCard(edge, nearFrom, where);
			} else
				_ClearCard();
			Invalidate();
		}
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_MOUSE_WHEEL_CHANGED && Wheel(message))
			return;
		BView::MessageReceived(message);
	}

	/** The wheel zooms (around the mouse); false if the message turns nothing. */
	bool Wheel(const BMessage* message)
	{
		float delta = 0;
		int32 steps;
		if (message->FindFloat("be:wheel_delta_y", &delta) != B_OK && message->FindInt32("be:wheel_delta_y", &steps) == B_OK)
			delta = steps;
		if (delta == 0)
			return false;
		BPoint where;
		uint32 buttons;
		GetMouse(&where, &buttons, false);
		if (!Bounds().Contains(where))
			where = BPoint(Bounds().Width() / 2, Bounds().Height() / 2);
		_Zoom(where, powf(1.15f, -delta));
		return true;
	}

	/** A view that has been resized shows what it showed: the whole graph again if it was fitted, else around the same middle. */
	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		if (fSize.width > 0 && !fAutoFit) {
			fOffset.x += (width - fSize.width) / 2;
			fOffset.y += (height - fSize.height) / 2;
		} else if (fAutoFit)
			fNeedFit = true;
		fSize = BSize(width, height);
		Invalidate();
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		if (count != 1) {
			BView::KeyDown(bytes, count);
			return;
		}
		switch (bytes[0]) {
			case 'f':
			case 'F':
				fNeedFit = true;
				Invalidate();
				break;
			case '+':
			case '=':
				_Zoom(BPoint(Bounds().Width() / 2, Bounds().Height() / 2), 1.25f);
				break;
			case '-':
				_Zoom(BPoint(Bounds().Width() / 2, Bounds().Height() / 2), 0.8f);
				break;
			default:
				BView::KeyDown(bytes, count);
		}
	}

private:
	BPoint _ToView(const Node& node) const
	{
		return BPoint(node.x * fScale + fOffset.x, node.y * fScale + fOffset.y);
	}

	/** the size of a node on the screen: the zoom does not make the nodes as big as the distances */
	float _Radius(const Node& node) const
	{
		return sen::graph::NodeRadiusAt(node.degree, fScale);
	}

	// ---- the card of what is under the mouse

	void _ClearCard()
	{
		delete fCard;
		fCard = NULL;
		fCardKind = kNoCard;
		fCardIndex = -1;
		fCardHot = false;
	}

	BRect _CardFrame() const
	{
		return fCard != NULL ? fCard->Frame(const_cast<GraphView*>(this), Bounds(), fCardAnchor, fCardDistance, fCardAlign) : BRect();
	}

	/** whether a point is on the card, on the object that it belongs to, or between the two */
	bool _InCardZone(BPoint where) const
	{
		const BRect frame = _CardFrame();
		if (frame.InsetByCopy(-4, -4).Contains(where))
			return true;
		if (hypotf(where.x - fCardAnchor.x, where.y - fCardAnchor.y) <= fCardDistance)
			return true;
		// the way from the anchor to the card
		BRect corridor = frame.left >= fCardAnchor.x
			? BRect(fCardAnchor.x - 6, fCardAnchor.y - 10, frame.left + 2, fCardAnchor.y + 10)
			: BRect(frame.right - 2, fCardAnchor.y - 10, fCardAnchor.x + 6, fCardAnchor.y + 10);
		return corridor.Contains(where);
	}

	void _ShowNodeCard(int32 node)
	{
		const Node& object = fGraph.Nodes()[node];
		_ClearCard();
		sen::EntityCard* card = fLoadCard ? fLoadCard(object.id) : NULL;
		if (card == NULL) {
			// no file to read (or no way to find it): what the graph knows
			card = new sen::EntityCard();
			card->SetTitle(object.label.c_str());
			if (!object.group.empty())
				card->AddRow("Type", object.group.c_str());
		}
		BString relations;
		relations << object.degree << (object.degree == 1 ? " relation" : " relations");
		card->SetFooter(relations.String());

		fCard = card;
		fCardKind = kNodeCard;
		fCardIndex = node;
		fCardAnchor = _ToView(object);
		fCardDistance = _Radius(object) + 10;
		fCardAlign = sen::EntityCard::kCentred;
		fCardHot = false;
	}

	/** how many objects a node is related to by a type: the lines of that type at the node */
	int32 _TargetsAt(int32 node, const std::string& type) const
	{
		int32 count = 0;
		for (const Edge& edge : fGraph.Edges()) {
			if (edge.type == type && (edge.from == node || edge.to == node))
				count++;
		}
		return count;
	}

	/** the card of a relation: its icon and its label as seen from the end that the mouse is nearer to, the two objects, and how many targets that end has */
	void _ShowEdgeCard(int32 index, bool nearFrom, BPoint where)
	{
		const Edge& edge = fGraph.Edges()[index];
		const BPoint a = _ToView(fGraph.Nodes()[edge.from]), b = _ToView(fGraph.Nodes()[edge.to]);

		// the point of the line that is nearest to the mouse: the card is next to it
		const float dx = b.x - a.x, dy = b.y - a.y;
		const float length2 = dx * dx + dy * dy;
		float t = length2 > 0.0001f ? ((where.x - a.x) * dx + (where.y - a.y) * dy) / length2 : 0;
		t = std::min(std::max(t, 0.0f), 1.0f);
		const BPoint anchor(a.x + t * dx, a.y + t * dy);

		if (fCardKind == kEdgeCard && fCardIndex == index && fCardNearFrom == nearFrom) {
			fCardAnchor = anchor;	// (the same card follows the mouse along the line)
			return;
		}

		_ClearCard();
		const Node& near = fGraph.Nodes()[nearFrom ? edge.from : edge.to];
		const Node& other = fGraph.Nodes()[nearFrom ? edge.to : edge.from];
		sen::EntityCard* card = new sen::EntityCard();
		// the label of the relation from the nearer end: the inverse one at the other end
		const std::string& label = nearFrom || edge.inverseLabel.empty() ? edge.label : edge.inverseLabel;
		BString title(label.c_str());
		if (title.IsEmpty())
			title = ShortTypeName(edge.type);
		card->SetTitle(title.String());
		card->AddRow("From", near.label.c_str());
		card->AddRow("To", other.label.c_str());
		// the icon of the relation type, else the one that all relations have (as the folders of the relations in Akita)
		BBitmap* icon = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
		if (!icon->IsValid() || (BMimeType(edge.type.c_str()).GetIcon(icon, B_LARGE_ICON) != B_OK
				&& BMimeType(sen::onto::core::mime::kContains).GetIcon(icon, B_LARGE_ICON) != B_OK)) {
			delete icon;
			icon = NULL;
		}
		card->SetPicture(icon);
		const int32 targets = _TargetsAt(nearFrom ? edge.from : edge.to, edge.type);
		BString count;
		count << targets << (targets == 1 ? " target" : " targets");
		card->SetFooter(count.String());

		fCard = card;
		fCardKind = kEdgeCard;
		fCardIndex = index;
		fCardNearFrom = nearFrom;
		fCardAnchor = anchor;
		fCardDistance = 12;
		// the card is beside the line and not over it: a line that leans to the right going down has its lower part under a card that is on the right,
		// so the card is above the point (and below it for the other lean), and the mouse can go on along the line to either end
		fCardAlign = dx * dy > 0 ? sen::EntityCard::kAbove : sen::EntityCard::kBelow;
		fCardHot = false;
	}

	/** what a click on the card does: the same as a double click on the object, or on the line (at the end that the card is of) */
	void _OpenCard()
	{
		if (fCardKind == kNodeCard) {
			_Select(fCardIndex);
			BMessage open(kOpenNode);
			open.AddString("id", fGraph.Nodes()[fCardIndex].id.c_str());
			Window()->PostMessage(&open);
		} else if (fCardKind == kEdgeCard) {
			const Edge& edge = fGraph.Edges()[fCardIndex];
			_SelectEdge(fCardIndex);
			BMessage open(kOpenRelations);
			open.AddString("id", fGraph.Nodes()[fCardNearFrom ? edge.from : edge.to].id.c_str());
			open.AddString("type", edge.type.c_str());
			Window()->PostMessage(&open);
		}
	}

	int32 _NodeAt(BPoint where) const
	{
		const std::vector<Node>& nodes = fGraph.Nodes();
		int32 found = -1;
		float best = 1e9f;
		for (size_t i = 0; i < nodes.size(); i++) {
			BPoint at = _ToView(nodes[i]);
			float distance = hypotf(at.x - where.x, at.y - where.y);
			if (distance <= _Radius(nodes[i]) + 3 && distance < best) {
				best = distance;
				found = static_cast<int32>(i);
			}
		}
		return found;
	}

	/** the line under a point (not at a node), and whether the point is nearer to the first of its nodes */
	int32 _EdgeAt(BPoint where, bool* nearFrom) const
	{
		const std::vector<Node>& nodes = fGraph.Nodes();
		const std::vector<Edge>& edges = fGraph.Edges();
		int32 found = -1;
		float best = 6.0f;	// the pixels around a line that are the line
		for (size_t i = 0; i < edges.size(); i++) {
			const BPoint a = _ToView(nodes[edges[i].from]), b = _ToView(nodes[edges[i].to]);
			const float distance = DistanceToSegment(where, a, b);
			if (distance < best) {
				best = distance;
				found = static_cast<int32>(i);
				*nearFrom = hypotf(where.x - a.x, where.y - a.y) <= hypotf(where.x - b.x, where.y - b.y);
			}
		}
		return found;
	}

	void _SelectEdge(int32 edge)
	{
		fSelected = -1;
		fSelectedEdge = edge;
		fNeighbors.assign(fGraph.Nodes().size(), false);
		if (edge >= 0) {
			fNeighbors[fGraph.Edges()[edge].from] = true;
			fNeighbors[fGraph.Edges()[edge].to] = true;
		}
		_SelectionChanged();
		Invalidate();
	}

	void _Select(int32 node)
	{
		fSelectedEdge = -1;
		fSelected = node;
		fNeighbors.assign(fGraph.Nodes().size(), false);
		if (node >= 0) {
			for (const Edge& edge : fGraph.Edges()) {
				if (edge.from == node)
					fNeighbors[edge.to] = true;
				else if (edge.to == node)
					fNeighbors[edge.from] = true;
			}
		}
		_SelectionChanged();
		Invalidate();
	}

	void _SelectionChanged()
	{
		if (Window() != NULL)
			Window()->PostMessage(kSelectionChanged);
	}

	void _Zoom(BPoint where, float factor)
	{
		_ClearCard();
		float scale = std::min(std::max(fScale * factor, 0.05f), 6.0f);
		factor = scale / fScale;
		fAutoFit = false;
		fOffset.x = where.x - (where.x - fOffset.x) * factor;
		fOffset.y = where.y - (where.y - fOffset.y) * factor;
		fScale = scale;
		Invalidate();
	}

	void _Fit()
	{
		float left, top, right, bottom;
		fGraph.Bounds(&left, &top, &right, &bottom);
		const float margin = 50;
		BRect bounds(Bounds());
		float width = std::max(right - left, 1.0f), height = std::max(bottom - top, 1.0f);
		fScale = std::min(std::max(bounds.Width() - 2 * margin, 50.0f) / width, std::max(bounds.Height() - 2 * margin, 50.0f) / height);
		fScale = std::min(fScale, 2.5f);
		fOffset.x = bounds.Width() / 2 - (left + width / 2) * fScale;
		fOffset.y = bounds.Height() / 2 - (top + height / 2) * fScale;
		fNeedFit = false;
		fAutoFit = true;
	}

	sen::graph::Graph	fGraph;
	std::vector<bool>	fNeighbors;		///< the nodes that the selected one is connected to
	BString				fMessage;
	float				fScale;
	BPoint				fOffset;
	bool				fNeedFit;
	bool				fAutoFit;		///< the graph is fitted into the view, and stays so when the view is resized (until it is zoomed or moved)
	BSize				fSize;
	int32				fHover;
	int32				fSelected;
	enum { kNoCard, kNodeCard, kEdgeCard };
	sen::EntityCard*	fCard;			///< the card of what is under the mouse (an object or a line), drawn next to it with a translucent background
	int32				fCardKind;
	int32				fCardIndex;		///< the node or the line that the card is of
	bool				fCardNearFrom;	///< for a line: the card is of the first of its nodes (else of the second)
	BPoint				fCardAnchor;	///< where the card belongs to (the middle of the node, a point of the line)
	float				fCardDistance;	///< how far from the anchor the card starts
	sen::EntityCard::Align	fCardAlign;	///< beside the anchor: in the middle of it, or above or below it (a line must not run under the card)
	bool				fCardHot;		///< the mouse is on the card: it lights up
	bool				fOverlays;
	std::function<sen::EntityCard*(const std::string&)>	fLoadCard;
	int32				fHoverEdge;
	int32				fSelectedEdge;
	BPoint				fMouse;
	BPoint				fLast;
	bool				fDragging;
	bool				fDark;
	std::vector<bool>	fNodeLabelShown;	///< what the last drawing showed (the SVG has the same)
	std::vector<bool>	fEdgeLabelShown;
};


void
GraphView::Draw(BRect)
{
	const sen::graph::Palette palette = sen::graph::PaletteFor(fDark);
	const rgb_color background = Color(palette.background);
	const rgb_color textColor = Color(palette.text);
	SetLowColor(background);
	FillRect(Bounds(), B_SOLID_LOW);

	BFont font(be_plain_font);
	SetFont(&font);
	font_height height;
	font.GetHeight(&height);

	const std::vector<Node>& nodes = fGraph.Nodes();
	if (nodes.empty()) {
		SetHighColor(Color(palette.edgeText));
		BRect bounds(Bounds());
		BString text(fMessage.IsEmpty() ? "No objects with relations" : fMessage.String());
		DrawString(text.String(), BPoint((bounds.Width() - font.StringWidth(text.String())) / 2, bounds.Height() / 2));
		return;
	}

	if (fNeedFit)
		_Fit();

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	const bool selectingNode = fSelected >= 0, selectingEdge = fSelectedEdge >= 0;
	const bool selecting = selectingNode || selectingEdge;
	const bool manyNodes = nodes.size() > 60;
	const std::vector<Edge>& edges = fGraph.Edges();

	// is a line one of those that are emphasised: the ones of the selected node, the selected line, the one under the mouse
	auto emphasised = [&](size_t index) {
		const Edge& edge = edges[index];
		return (selectingNode && (edge.from == fSelected || edge.to == fSelected)) || static_cast<int32>(index) == fSelectedEdge
			|| static_cast<int32>(index) == fHoverEdge;
	};

	// the edges: a line to the rim of the node, an arrow if the relation is in one direction only
	for (size_t index = 0; index < edges.size(); index++) {
		const Edge& edge = edges[index];
		const bool strong = emphasised(index);
		const bool active = !selecting || strong;
		const Node& from = nodes[edge.from];
		const Node& to = nodes[edge.to];
		BPoint a = _ToView(from), b = _ToView(to);
		float dx = b.x - a.x, dy = b.y - a.y;
		float length = std::max(hypotf(dx, dy), 0.01f);
		dx /= length;
		dy /= length;
		const float startRadius = _Radius(from), endRadius = _Radius(to) + (edge.reciprocal ? 0 : 1);
		BPoint start(a.x + dx * startRadius, a.y + dy * startRadius), end(b.x - dx * endRadius, b.y - dy * endRadius);

		rgb_color color = Color(edge.meta ? palette.metaEdge : palette.edge);
		if (strong)
			color = Color(palette.emphasis);
		color.alpha = active ? 255 : 50;
		SetHighColor(color);
		SetPenSize(strong ? sen::graph::kStrongLineWidth : sen::graph::kLineWidth);
		StrokeLine(start, end);

		if (!edge.reciprocal && length > startRadius + endRadius + 8) {
			const float size = 7;
			BPoint left(end.x - dx * size - dy * size / 2.5f, end.y - dy * size + dx * size / 2.5f);
			BPoint right(end.x - dx * size + dy * size / 2.5f, end.y - dy * size - dx * size / 2.5f);
			FillTriangle(end, left, right);
		}
	}

	// Which labels are drawn. The wanted ones are placed by their importance, and one that would lie on another that is placed is left out: the node that is
	// selected or under the mouse, the nodes it is connected to, the selected line, then the lines of the selected node (if it has not many), the hubs,
	// the rest.
	std::vector<bool> edgeLabelShown(edges.size(), false), nodeLabelShown(nodes.size(), false);
	std::vector<BString> nodeTexts(nodes.size()), edgeTexts(edges.size());
	{
		struct Candidate {
			bool	isNode;
			size_t	index;
			int		priority;
			BRect	rect;
		};
		std::vector<Candidate> candidates;
		const int selectedDegree = selectingNode ? nodes[fSelected].degree : 0;

		for (size_t i = 0; i < nodes.size(); i++) {
			const Node& node = nodes[i];
			const bool chosen = static_cast<int32>(i) == fSelected || static_cast<int32>(i) == fHover;
			const bool related = selecting && fNeighbors[i];
			bool wanted = chosen || related;
			if (!wanted && !selecting)
				wanted = !manyNodes || fScale >= 1.0f || (node.degree >= 3 && fScale >= 0.4f);
			if (!wanted)
				continue;
			BString label(node.label.c_str());
			if (label.CountChars() > 28)
				label.TruncateChars(27).Append(B_UTF8_ELLIPSIS);
			nodeTexts[i] = label;
			const BPoint at = _ToView(node);
			const float width = font.StringWidth(label.String());
			const BPoint where(at.x - width / 2, at.y + _Radius(node) + height.ascent + 2);
			Candidate candidate = {true, i, chosen ? 0 : (related ? 1 : (node.degree >= 3 ? 4 : 6)),
				BRect(where.x - 2, where.y - height.ascent, where.x + width + 2, where.y + height.descent)};
			candidates.push_back(candidate);
		}

		for (size_t index = 0; index < edges.size(); index++) {
			const Edge& edge = edges[index];
			const bool direct = static_cast<int32>(index) == fSelectedEdge || static_cast<int32>(index) == fHoverEdge;
			const bool ofSelected = selectingNode && (edge.from == fSelected || edge.to == fSelected) && selectedDegree <= 8;
			bool wanted = direct || ofSelected;
			if (!wanted && !selecting)
				wanted = !((manyNodes && fScale < 1.2f) || fScale < 0.7f);
			if (!wanted)
				continue;
			BString label = EdgeLabel(edge);
			if (label.IsEmpty())
				continue;
			edgeTexts[index] = label;
			const BPoint a = _ToView(nodes[edge.from]), b = _ToView(nodes[edge.to]);
			const float width = font.StringWidth(label.String());
			const BPoint at((a.x + b.x) / 2 - width / 2, (a.y + b.y) / 2 + height.ascent / 2 - 1);
			Candidate candidate = {false, index, direct ? 0 : (ofSelected ? 2 : 5),
				BRect(at.x - 2, at.y - height.ascent, at.x + width + 2, at.y + height.descent)};
			candidates.push_back(candidate);
		}

		std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
		std::vector<BRect> placed;
		for (const Candidate& candidate : candidates) {
			bool free = Bounds().Intersects(candidate.rect);
			for (size_t i = 0; free && i < placed.size(); i++)
				free = !placed[i].Intersects(candidate.rect);
			if (!free)
				continue;
			placed.push_back(candidate.rect);
			(candidate.isNode ? nodeLabelShown : edgeLabelShown)[candidate.index] = true;
		}
	}

	fNodeLabelShown = nodeLabelShown;
	fEdgeLabelShown = edgeLabelShown;

	// the labels of the edges
	for (size_t index = 0; index < edges.size(); index++) {
		if (!edgeLabelShown[index])
			continue;
		const Edge& edge = edges[index];
		const bool strong = selecting ? emphasised(index) : static_cast<int32>(index) == fHoverEdge;
		BPoint a = _ToView(nodes[edge.from]), b = _ToView(nodes[edge.to]);
		float width = font.StringWidth(edgeTexts[index].String());
		BPoint at((a.x + b.x) / 2 - width / 2, (a.y + b.y) / 2 + height.ascent / 2 - 1);
		rgb_color halo = background;
		halo.alpha = 200;
		SetHighColor(halo);
		FillRect(BRect(at.x - 2, at.y - height.ascent, at.x + width + 2, at.y + height.descent));
		SetHighColor(strong ? textColor : Color(palette.edgeText));
		DrawString(edgeTexts[index].String(), at);
	}

	// the nodes
	for (size_t i = 0; i < nodes.size(); i++) {
		const Node& node = nodes[i];
		const bool dimmed = selecting && static_cast<int32>(i) != fSelected && !fNeighbors[i];
		const bool strong = static_cast<int32>(i) == fSelected || static_cast<int32>(i) == fHover;
		BPoint at = _ToView(node);
		float radius = _Radius(node) + (strong ? 2 : 0);

		rgb_color color = Color(sen::graph::GroupColor(node.group, fDark));
		color.alpha = dimmed ? 90 : 255;
		SetHighColor(color);
		FillEllipse(at, radius, radius);
		rgb_color ring = strong ? Color(palette.emphasis) : background;
		ring.alpha = dimmed ? 90 : 255;
		SetHighColor(ring);
		SetPenSize(strong ? 2.0f : 1.2f);
		StrokeEllipse(at, radius, radius);
	}

	// the names
	for (size_t i = 0; i < nodes.size(); i++) {
		if (!nodeLabelShown[i])
			continue;
		const Node& node = nodes[i];
		BPoint at = _ToView(node);
		float width = font.StringWidth(nodeTexts[i].String());
		BPoint where(at.x - width / 2, at.y + _Radius(node) + height.ascent + 2);
		rgb_color halo = background;
		halo.alpha = 210;
		SetHighColor(halo);
		FillRect(BRect(where.x - 2, where.y - height.ascent, where.x + width + 2, where.y + height.descent));
		SetHighColor(textColor);
		DrawString(nodeTexts[i].String(), where);
	}

	// the card of what is under the mouse: over everything, and the graph shows through
	if (fCard != NULL)
		fCard->Draw(this, Bounds(), fCardAnchor, fCardDistance, fDark, fCardHot, fCardAlign);
}


// ---- the window

struct WorkRequest {
	bool								collect;
	bool								withMeta;
	bool								withUnconnected;
	bool								withOntologies;
	sen::graph::Graph					all;
	std::map<std::string, entry_ref>	refs;
	bool								truncated;
	int32								objects;
};


struct WorkResult {
	sen::graph::Graph					shown;
	sen::graph::Graph					all;
	std::map<std::string, entry_ref>	refs;
	bool								truncated;
	int32								objects;
	status_t							error;
};


GraphWindow::GraphWindow(SenConfigHandler* config)
	:
	BWindow(BRect(100, 100, 1060, 740), "SEN Graph", B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fSavePanel(NULL),
	fConfig(config),
	fSelectionOnly(false),
	fWorker(new Worker("SEN graph")),
	fWorking(false),
	fTruncated(false),
	fObjects(0),
	fHasFocus(false)
{
	fView = new GraphView();
	fView->SetCardLoader([this](const std::string& id) { return _LoadCard(id); });
	fMeta = new BCheckBox("meta", "Classifications and contexts", new BMessage(kFilter));
	fUnconnected = new BCheckBox("unconnected", "Objects without relations", new BMessage(kFilter));
	fOntologies = new BCheckBox("ontologies", "Ontologies", new BMessage(kFilter));
	fDark = new BCheckBox("dark", "Dark", new BMessage(kDark));
	fPreview = new BCheckBox("preview", "Preview", new BMessage(kPreviewToggled));
	fPreview->SetValue(B_CONTROL_ON);
	// dark or light as it was chosen last time (a field of the settings), else as the colours of the system are
	bool dark = false;
	BMessage settings;
	if (fConfig != NULL && fConfig->GetConfig(&settings) == B_OK && settings.FindBool(sen::key::kDarkMode, &dark) == B_OK)
		fView->SetDark(dark);
	fDark->SetValue(fView->Dark() ? B_CONTROL_ON : B_CONTROL_OFF);
	fRefresh = new BButton("refresh", "Refresh", new BMessage(kRefresh));
	BButton* save = new BButton("save", "Save as SVG" B_UTF8_ELLIPSIS, new BMessage(kSave));
	fSaveSelection = new BButton("saveSelection", "Save selection" B_UTF8_ELLIPSIS, new BMessage(kSaveSelection));
	fSaveSelection->SetEnabled(false);
	fStatus = new BStringView("status", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(fMeta)
			.Add(fUnconnected)
			.Add(fOntologies)
			.Add(fPreview)
			.Add(fDark)
			.AddGlue()
			.Add(fSaveSelection)
			.Add(save)
			.Add(fRefresh)
		.End()
		.Add(fView, 10.0f)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fStatus)
			.AddGlue()
			.AddStrut(B_V_SCROLL_BAR_WIDTH)	// (the resize handle of a document window is in the corner)
		.End()
	.End();

	fView->SetMessage("Reading the objects" B_UTF8_ELLIPSIS);
	_Start(true);
}


GraphWindow::~GraphWindow()
{
	// (the worker quits when the work that it is doing is done: the window does not wait for it)
	fWorker->Finish();
	delete fSavePanel;
}


/** The card of what is under the mouse (an object: what Shoji would show of it, a line each, read-only; a relation: its icon and label) is drawn by the view over the
 *  graph; the window knows the file of a node, the checkbox switches the cards off. */
void
GraphWindow::_UpdatePreview()
{
	fView->SetOverlays(fPreview->Value() == B_CONTROL_ON);
}


/** The card of the file of a node, NULL if the node is no file that is known or its card cannot be made. */
sen::EntityCard*
GraphWindow::_LoadCard(const std::string& id) const
{
	auto found = fRefs.find(id);
	if (found == fRefs.end())
		return NULL;

	sen::EntityCard* card = new sen::EntityCard();
	if (card->Load(found->second) != B_OK) {
		delete card;
		return NULL;
	}
	return card;
}


void
GraphWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	// the wheel goes to the view that has the focus (a checkbox at first), not to the one under the mouse: it zooms the graph when the mouse is over it
	if (message->what == B_MOUSE_WHEEL_CHANGED && fView != NULL) {
		BPoint where;
		uint32 buttons;
		fView->GetMouse(&where, &buttons, false);
		if (fView->Bounds().Contains(where) && fView->Wheel(message))
			return;
	}
	BWindow::DispatchMessage(message, handler);
}


void
GraphWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kRefresh:
			_Start(true);
			break;
		case kFilter:
			_Start(false);
			break;
		case kDark:
		{
			const bool dark = fDark->Value() == B_CONTROL_ON;
			fView->SetDark(dark);
			if (fConfig != NULL) {
				BMessage change;
				change.AddBool(sen::key::kDarkMode, dark);
				fConfig->UpdateSettings(&change);	// (remembered: the next window is as this one)
			}
			break;
		}
		case kSelectionChanged:
			fSaveSelection->SetEnabled(fView->HasSelection());
			break;
#ifdef SEN_TESTING
		case kTestMouse:
		{
			BPoint screen;
			if (message->FindPoint("screen_where", &screen) == B_OK)
				fView->TestMouse(fView->ConvertFromScreen(screen), message->GetBool("down", false));
			break;
		}
#endif
		case kPreviewToggled:
			_UpdatePreview();
			break;
		case kWorkDone:
			_Done(message);
			break;
		case kOpenNode:
			_Open(message);
			break;
		case kOpenRelations:
			_OpenRelations(message);
			break;
		case kSelectNode:
			fView->SelectId(message->GetString("id", ""));
			break;
		case kGraphFocus:
			if (message->FindRef("refs", &fFocusRef) == B_OK) {
				fHasFocus = true;
				_ApplyFocus();	// (at once if the graph is there, else when it is)
			}
			break;
		case kSave:
		case kSaveSelection:
		case B_SAVE_REQUESTED:
			_Save(message);
			break;
		case kActivate:
			Activate();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


/** what the worker does: read the objects (if asked), filter and lay out the graph; the result goes to the window as a pointer. Takes over the request. */
static BMessage*
RunWork(WorkRequest* request)
{
	WorkResult* result = new WorkResult;
	result->error = B_OK;
	result->truncated = request->truncated;
	result->objects = request->objects;

	if (request->collect) {
		sen::graph::Collected collected;
		result->error = sen::graph::Collect(&collected, kMaxObjects);
		result->all = collected.graph;
		result->refs = collected.refs;
		result->truncated = collected.truncated;
		result->objects = collected.objects;
	} else {
		result->all = request->all;
		result->refs = request->refs;
	}

	result->shown = result->all.Filtered(request->withMeta, request->withUnconnected, request->withOntologies);
	result->shown.Layout();

	BMessage* done = new BMessage(kWorkDone);
	done->AddPointer("result", result);
	delete request;
	return done;
}


void
GraphWindow::_Start(bool collect)
{
	if (fWorking)
		return;
	fWorking = true;
	fRefresh->SetEnabled(false);
	fStatus->SetText(collect ? "Reading the objects" B_UTF8_ELLIPSIS : "Laying out" B_UTF8_ELLIPSIS);

	WorkRequest* request = new WorkRequest;
	request->collect = collect;
	request->withMeta = fMeta->Value() == B_CONTROL_ON;
	request->withUnconnected = fUnconnected->Value() == B_CONTROL_ON;
	request->withOntologies = fOntologies->Value() == B_CONTROL_ON;
	request->truncated = fTruncated;
	request->objects = fObjects;
	if (!collect) {
		request->all = fAll;
		request->refs = fRefs;
	}

	// (the result of a window that is gone is freed here)
	status_t result = fWorker->Post([request]() { return RunWork(request); }, BMessenger(this), [](BMessage* message) {
		WorkResult* done = NULL;
		if (message->FindPointer("result", (void**)&done) == B_OK)
			delete done;
	});
	if (result != B_OK) {
		delete request;
		fWorking = false;
		fRefresh->SetEnabled(true);
		fStatus->SetText("Could not start the work");
	}
}


void
GraphWindow::_Done(BMessage* message)
{
	WorkResult* result = NULL;
	if (message->FindPointer("result", (void**)&result) != B_OK || result == NULL)
		return;
	fWorking = false;
	fRefresh->SetEnabled(true);

	if (result->error != B_OK && result->all.Nodes().empty()) {
		fView->SetGraph(sen::graph::Graph());
		fView->SetMessage("The objects could not be read");
		BString status("Could not read the objects: ");
		status << strerror(result->error);
		fStatus->SetText(status.String());
		delete result;
		return;
	}

	fAll = result->all;
	fRefs = result->refs;
	fTruncated = result->truncated;
	fObjects = result->objects;

	if (result->shown.Nodes().empty()) {
		fView->SetMessage(result->all.Nodes().empty() ? "There are no objects yet" : "No object has a relation yet (see the choices above)");
	}
	fView->SetGraph(result->shown);

	BString status;
	status << static_cast<int32>(result->shown.Nodes().size()) << " of " << static_cast<int32>(fAll.Nodes().size()) << " objects, "
		<< static_cast<int32>(result->shown.Edges().size()) << " relations";
	if (fTruncated)
		status << " (the first " << static_cast<int32>(kMaxObjects) << " of " << fObjects << " objects were read)";
	status << ".  Click: select, double click: open (a line: its relations), wheel: zoom, F: fit.";
	fStatus->SetText(status.String());
	delete result;
	_ApplyFocus();
}


void
GraphWindow::_ApplyFocus()
{
	if (!fHasFocus || fWorking)
		return;

	std::string id;
	for (const auto& object : fRefs) {
		if (object.second == fFocusRef) {
			id = object.first;
			break;
		}
	}
	if (id.empty()) {
		// not an object of SEN (it has no SEN:ID), or not read
		fHasFocus = false;
		BString status(fFocusRef.name);
		status << " is no object of SEN: it has no relations.";
		fStatus->SetText(status.String());
		return;
	}
	if (fView->Graph().IndexOf(id) < 0) {
		// it is in the graph but not shown (no relations, only the classifications and contexts, or it is an ontology): show everything, then centre on it
		if (fAll.IndexOf(id) >= 0 && (fMeta->Value() != B_CONTROL_ON || fUnconnected->Value() != B_CONTROL_ON
				|| fOntologies->Value() != B_CONTROL_ON)) {
			fMeta->SetValue(B_CONTROL_ON);
			fUnconnected->SetValue(B_CONTROL_ON);
			fOntologies->SetValue(B_CONTROL_ON);
			_Start(false);
			return;
		}
		fHasFocus = false;
		return;
	}
	fHasFocus = false;
	fView->Focus(id.c_str());
}


void
GraphWindow::_Open(const BMessage* message)
{
	auto found = fRefs.find(message->GetString("id", ""));
	if (found == fRefs.end())
		return;
	// as a double click in Tracker does (what Akita shows for an ontology or an entity is what it does then)
	BMessage open(B_REFS_RECEIVED);
	open.AddRef("refs", &found->second);
	BMessenger(kTrackerSignature).SendMessage(&open);
}


void
GraphWindow::_OpenRelations(const BMessage* message)
{
	auto found = fRefs.find(message->GetString("id", ""));
	if (found == fRefs.end())
		return;
	// Akita shows the folder of the relations of that type of the file (as the menu Open related... does), or all of them if the type is not known
	const char* type = message->GetString("type", "");
	BMessage open(type[0] != '\0' ? sen::cmd::kOpenRelationsOfType : sen::cmd::kOpenAllRelationsView);
	open.AddRef(sen::key::kSourceRef, &found->second);
	if (type[0] != '\0')
		open.AddString(sen::key::kRelationType, type);
	BMessenger(kTrackerSignature).SendMessage(&open);
}


void
GraphWindow::_Save(const BMessage* message)
{
	if (message->what == kSave || message->what == kSaveSelection) {
		fSelectionOnly = message->what == kSaveSelection;
		if (fSavePanel == NULL) {
			BMessenger target(this);
			fSavePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, B_FILE_NODE, false);
		}
		BString name("sen-graph");
		if (fSelectionOnly)
			name << "-" << fView->SelectionName();
		name << ".svg";
		fSavePanel->SetSaveText(name.String());
		fSavePanel->Show();
		return;
	}

	entry_ref directory;
	const char* name;
	if (message->FindRef("directory", &directory) != B_OK || message->FindString("name", &name) != B_OK)
		return;

	// the picture of the view as it is seen, or of the selection (a node with its direct connections)
	const std::string svg = fView->Svg(fSelectionOnly);
	if (svg.empty()) {
		BAlert* alert = new BAlert("SEN Graph", "There is nothing to save.", "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
		return;
	}
	BDirectory folder(&directory);
	BFile file(&folder, name, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t result = file.InitCheck();
	if (result == B_OK) {
		ssize_t written = file.Write(svg.data(), svg.size());
		result = written == static_cast<ssize_t>(svg.size()) ? B_OK : (written < 0 ? written : B_ERROR);
	}
	if (result == B_OK) {
		BNodeInfo(&file).SetType("image/svg+xml");
		BString status("Saved as ");
		status << name;
		fStatus->SetText(status.String());
	} else {
		BString text("The graph could not be saved: ");
		text << strerror(result);
		BAlert* alert = new BAlert("SEN Graph", text.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
	}
}

}	// namespace ui
}	// namespace sen
