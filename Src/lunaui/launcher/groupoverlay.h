/* @@@LICENSE
*
*      Copyright (c) 2026 webOS Archive
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#ifndef GROUPOVERLAY_H_
#define GROUPOVERLAY_H_

#include "thingpaintable.h"
#include "InputClient.h"

#include <QList>
#include <QPointer>
#include <QRectF>
#include <QFont>

class LauncherObject;
class ReorderablePage;
class GroupIcon;
class IconBase;
class QTapGesture;
class QTapAndHoldGesture;
class QGestureEvent;
class QKeyEvent;

/*
 * GroupOverlay - the floating panel that opens when an app group icon is tapped.
 * Shows the group name and the member icons in a grid; tap launches a member,
 * tap-and-hold pops the member back out of the group onto the page, tap outside
 * the panel dismisses.
 *
 * It covers the whole launcher area (shielding the page below from input) and
 * temporarily reparents the member icons to itself while showing.
 *
 * Tapping the title starts inline renaming: the overlay registers itself as the
 * IME's InputClient (which raises the virtual keyboard, whose keystrokes arrive
 * as QKeyEvents on the scene focus item = this overlay).
 */
class GroupOverlay : public ThingPaintable , public InputClient
{
	Q_OBJECT

public:

	GroupOverlay(const QRectF& geometry,LauncherObject * p_launcher);
	virtual ~GroupOverlay();

	void showForGroup(GroupIcon * p_groupIcon,ReorderablePage * p_ownerPage);
	void hideOverlay();
	bool isShowing() const;
	GroupIcon * currentGroup() const;
	ReorderablePage * ownerPage() const;

	//re-read membership from the group and lay the panel out again
	void relayoutMembers();

	//drop the icon from the overlay's shown list WITHOUT touching its visual state;
	// must be called before handing a popped-out icon to a new owner, so later
	// relayout/release passes don't disturb it
	void forgetIcon(IconBase * p_icon);

	//////// inline group-name editing ////////
	bool isEditingName() const;
	void startNameEdit();
	void commitNameEdit();
	void cancelNameEdit();

	// InputClient - virtual keyboard hookup
	virtual void commitText(const std::string& text);
	virtual void performEditorAction(PalmIME::FieldAction action);

Q_SIGNALS:

	void signalMemberIconTapped(IconBase * p_icon);
	void signalMemberIconPoppedOut(IconBase * p_icon);
	void signalDismissed();

protected:

	virtual bool sceneEvent(QEvent * event);
	virtual bool tapGesture(QTapGesture * tapEvent,QGestureEvent * baseGestureEvent);
	virtual bool tapAndHoldGesture(QTapAndHoldGesture * tapHoldEvent,QGestureEvent * baseGestureEvent);
	virtual void keyPressEvent(QKeyEvent * event);

	void endNameEditCommon();

	virtual void paint(QPainter * painter,const QStyleOptionGraphicsItem * option=0,QWidget * widget=0);
	virtual void paintOffscreen(QPainter * painter);

	//releases the member icons back to invisible/no-parent state
	void releaseShownIcons();

	IconBase * memberAtPointICS(const QPointF& pointICS) const;

private:

	QPointer<LauncherObject> m_qp_launcher;
	QPointer<GroupIcon> m_qp_groupIcon;
	QPointer<ReorderablePage> m_qp_ownerPage;

	bool m_showing;
	bool m_editingName;
	QString m_editBuffer;
	QPointer<QGraphicsObject> m_qp_prevFocusItem;	//scene focus to restore after editing
	QRectF m_panelRect;		//ICS
	QRectF m_titleRect;		//ICS
	QFont m_titleFont;
	QList<QPointer<IconBase> > m_shownIcons;
	QList<QRectF> m_iconRects;	//ICS; parallel to m_shownIcons
};

#endif /* GROUPOVERLAY_H_ */
