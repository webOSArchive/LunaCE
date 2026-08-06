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
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QFont>

class LauncherObject;
class ReorderablePage;
class GroupIcon;
class IconBase;
class QTapGesture;
class QTapAndHoldGesture;
class QGestureEvent;
class QKeyEvent;
class QPropertyAnimation;

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
 *
 * Opening and closing are animated by the openProgress property (0 = gone,
 * 1 = fully open): the panel and its member icons scale up out of the group
 * icon that was tapped while the backdrop dim fades in, and reverse on close.
 *
 * A pop-out happens while the finger is still down, and it can take the panel
 * out from under that finger (a two-member group dissolves outright). So from
 * the moment an icon pops out until the touch is released, the overlay keeps
 * its (by then invisible) shield up and swallows everything: without that, the
 * release lands as a tap on whatever the panel was covering - typically the
 * page icon behind the group, which then launches.
 */
class GroupOverlay : public ThingPaintable , public InputClient
{
	Q_OBJECT

	Q_PROPERTY(qreal openProgress READ openProgress WRITE setOpenProgress)

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

	//keeps the shield covering the whole launcher when it is resized (e.g. rotation)
	virtual bool resize(quint32 newWidth,quint32 newHeight);

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
	//the ime taking the keyboard away ends the edit; without this the panel would sit
	// there "editing" with no keyboard, and holding on to focus and the keyboard grab
	virtual void removeInputFocus();

	//////// open/close animation ////////
	qreal openProgress() const;
	void setOpenProgress(qreal progress);

Q_SIGNALS:

	void signalMemberIconTapped(IconBase * p_icon);
	void signalMemberIconPoppedOut(IconBase * p_icon);
	void signalDismissed();

protected Q_SLOTS:

	void slotOpenCloseAnimationFinished();
	void slotInputSwallowSafetyTimeout();

protected:

	virtual bool sceneEvent(QEvent * event);
	virtual bool tapGesture(QTapGesture * tapEvent,QGestureEvent * baseGestureEvent);
	virtual bool tapAndHoldGesture(QTapAndHoldGesture * tapHoldEvent,QGestureEvent * baseGestureEvent);
	virtual void keyPressEvent(QKeyEvent * event);

	void endNameEditCommon();

	//(re)assert this panel as the ime's client, which raises the virtual keyboard
	void raiseKeyboardForEdit();

	virtual void paint(QPainter * painter,const QStyleOptionGraphicsItem * option=0,QWidget * widget=0);
	virtual void paintOffscreen(QPainter * painter);

	//releases the member icons back to invisible/no-parent state
	void releaseShownIcons();

	//the finger that popped an icon out is up (or gone): stop swallowing, and let go
	// of the shield if the panel isn't up any more
	void endInputSwallow();

	IconBase * memberAtPointICS(const QPointF& pointICS) const;

	//////// open/close animation ////////
	void startOpenCloseAnimation(bool opening);
	//ends a close that is still running (releases the icons, hides the item)
	void finishClose();
	//the point the panel grows out of/shrinks back into: the group icon, if it can be located
	void computeAnimationOrigin();
	qreal animationScale() const;
	void applyOpenProgressToIcons();

private:

	QPointer<LauncherObject> m_qp_launcher;
	QPointer<GroupIcon> m_qp_groupIcon;
	QPointer<ReorderablePage> m_qp_ownerPage;

	bool m_showing;
	bool m_closing;			//animating out; the group/page pointers are already cleared
	bool m_ignoreInputUntilRelease;	//an icon popped out; swallow input until the touch ends
	qreal m_openProgress;
	QPointer<QPropertyAnimation> m_qp_openCloseAnimation;
	QPointF m_originICS;
	QString m_titleText;	//cached: the group icon can go away mid-close
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
