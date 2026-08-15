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

#include "groupoverlay.h"
#include "dimensionslauncher.h"
#include "reorderablepage.h"
#include "groupicon.h"
#include "icon.h"
#include "IMEController.h"

#include <QPainter>
#include <QPen>
#include <QColor>
#include <QEvent>
#include <QGesture>
#include <QGestureEvent>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QTimer>

#include <cstring>
#include <glib.h>

//layout constants of the panel (pixels)
#define GROUP_OVERLAY_CELL_EDGE_PX		110
#define GROUP_OVERLAY_TITLE_HEIGHT_PX	44
#define GROUP_OVERLAY_PADDING_PX		18
#define GROUP_OVERLAY_MAX_COLUMNS		4
#define GROUP_OVERLAY_MAX_SHOWN			16
//how far the panel shifts up while the virtual keyboard is up for renaming
#define GROUP_OVERLAY_EDIT_SHIFT_UP_PX	140
#define GROUP_OVERLAY_MAX_NAME_LEN		24
//open/close animation: a short scale-and-fade out of (and back into) the group icon
#define GROUP_OVERLAY_OPEN_ANIM_MS		160
#define GROUP_OVERLAY_CLOSE_ANIM_MS		130
#define GROUP_OVERLAY_ANIM_MIN_SCALE	0.85
//backstop for the post-pop-out input swallow, in case the touch end never arrives:
// a shield that never lets go would leave the launcher dead to the touch
#define GROUP_OVERLAY_SWALLOW_SAFETY_MS	5000

GroupOverlay::GroupOverlay(const QRectF& geometry,LauncherObject * p_launcher)
: ThingPaintable(geometry)
, m_qp_launcher(p_launcher)
, m_qp_groupIcon(0)
, m_qp_ownerPage(0)
, m_showing(false)
, m_closing(false)
, m_ignoreInputUntilRelease(false)
, m_openProgress(0.0)
, m_editingName(false)
{
	m_titleFont = QFont(QString("Prelude"));
	m_titleFont.setPixelSize(22);
	m_titleFont.setBold(true);

	setAcceptTouchEvents(true);
	grabGesture(Qt::TapGesture);
	grabGesture(Qt::TapAndHoldGesture);
	//NOT focusable except while renaming (see startNameEdit): the panel covers the
	// whole launcher, and Qt hands scene focus to the topmost focusable item under a
	// press, so a focusable panel silently takes the focus the overlay window manager
	// is holding - and the window manager's "am I still focused?" teardown then never
	// runs when the launcher hands off to a card
	setFlag(QGraphicsItem::ItemIsFocusable,false);
	setZValue(100.0);
	setVisible(false);

	QPropertyAnimation * pAnim = new QPropertyAnimation(this,"openProgress",this);
	connect(pAnim,SIGNAL(finished()),this,SLOT(slotOpenCloseAnimationFinished()));
	m_qp_openCloseAnimation = pAnim;
}

//virtual
GroupOverlay::~GroupOverlay()
{
	if (m_editingName)
	{
		IMEController::instance()->removeClient(this);
	}
}

bool GroupOverlay::isShowing() const
{
	return m_showing;
}

GroupIcon * GroupOverlay::currentGroup() const
{
	return m_qp_groupIcon;
}

ReorderablePage * GroupOverlay::ownerPage() const
{
	return m_qp_ownerPage;
}

void GroupOverlay::showForGroup(GroupIcon * p_groupIcon,ReorderablePage * p_ownerPage)
{
	if (!p_groupIcon)
	{
		return;
	}
	if (m_showing)
	{
		hideOverlay();
	}
	//if a close was still animating out, complete it now rather than opening on top of it
	finishClose();
	m_qp_groupIcon = p_groupIcon;
	m_qp_ownerPage = p_ownerPage;
	m_titleText = p_groupIcon->groupName();
	m_showing = true;
	//a panel that is up does its own shielding; nothing left to swallow for a past pop-out
	m_ignoreInputUntilRelease = false;
	setAcceptTouchEvents(true);
	relayoutMembers();
	setVisible(true);
	startOpenCloseAnimation(true);
	update();
}

void GroupOverlay::hideOverlay()
{
	if (!m_showing)
	{
		return;
	}
	if (m_editingName)
	{
		commitNameEdit();
	}
	m_showing = false;
	m_closing = true;
	//stop shielding right away: the launcher is the user's again the moment the panel
	// starts going away, and the member icons are still children until the close finishes
	// ...unless this hide is a pop-out closing the panel under a finger that's still down,
	// in which case the shield stays until that touch ends (see endInputSwallow)
	if (!m_ignoreInputUntilRelease)
	{
		setAcceptTouchEvents(false);
	}
	//callers act on the group/page the moment they ask for the hide, so let go of them now
	m_qp_groupIcon = 0;
	m_qp_ownerPage = 0;
	Q_EMIT signalDismissed();
	startOpenCloseAnimation(false);
}

void GroupOverlay::releaseShownIcons()
{
	for (QList<QPointer<IconBase> >::iterator it = m_shownIcons.begin();
			it != m_shownIcons.end();++it)
	{
		if (!(*it))
		{
			continue;
		}
		//undo whatever the open/close animation left on the icon
		(*it)->setOpacity(1.0);
		(*it)->setScale(1.0);
		//and the tap flash a member launch leaves on (see slotGroupOverlayMemberTapped)
		(*it)->setLaunchFeedbackVisibility(false);
		if ((*it)->parentItem() != this)
		{
			//someone took it over while the panel was animating out (see forgetIcon);
			// it's theirs now, don't yank it out of the scene
			continue;
		}
		(*it)->slotDisableIconAutoRepaint();
		(*it)->setParentItem(0);
		if ((*it)->scene())
		{
			(*it)->scene()->removeItem(*it);
		}
	}
	m_shownIcons.clear();
	m_iconRects.clear();
}

qreal GroupOverlay::openProgress() const
{
	return m_openProgress;
}

void GroupOverlay::setOpenProgress(qreal progress)
{
	m_openProgress = qBound((qreal)0.0,progress,(qreal)1.0);
	applyOpenProgressToIcons();
	update();
}

qreal GroupOverlay::animationScale() const
{
	return GROUP_OVERLAY_ANIM_MIN_SCALE
			+ (1.0 - GROUP_OVERLAY_ANIM_MIN_SCALE) * m_openProgress;
}

void GroupOverlay::startOpenCloseAnimation(bool opening)
{
	if (!m_qp_openCloseAnimation)
	{
		//no animator (shouldn't happen): just snap
		setOpenProgress(opening ? 1.0 : 0.0);
		if (!opening)
		{
			finishClose();
		}
		return;
	}
	m_qp_openCloseAnimation->stop();
	m_qp_openCloseAnimation->setDuration(opening ? GROUP_OVERLAY_OPEN_ANIM_MS
											: GROUP_OVERLAY_CLOSE_ANIM_MS);
	m_qp_openCloseAnimation->setEasingCurve(opening ? QEasingCurve::OutCubic
											: QEasingCurve::InCubic);
	m_qp_openCloseAnimation->setStartValue(m_openProgress);
	m_qp_openCloseAnimation->setEndValue(opening ? 1.0 : 0.0);
	m_qp_openCloseAnimation->start();
}

//protected Q_SLOTS:
void GroupOverlay::slotOpenCloseAnimationFinished()
{
	if (m_closing)
	{
		finishClose();
	}
}

void GroupOverlay::finishClose()
{
	if (!m_closing)
	{
		return;
	}
	m_closing = false;
	if (m_qp_openCloseAnimation)
	{
		m_qp_openCloseAnimation->stop();
	}
	m_openProgress = 0.0;
	releaseShownIcons();
	m_titleText.clear();
	//an invisible item is out of the scene's reach, so a shield that's still waiting on a
	// touch release stays visible (it paints nothing once the close is done)
	if (!m_ignoreInputUntilRelease)
	{
		setVisible(false);
	}
	update();
}

void GroupOverlay::endInputSwallow()
{
	if (!m_ignoreInputUntilRelease)
	{
		return;
	}
	m_ignoreInputUntilRelease = false;
	if (m_showing)
	{
		//the panel is still up (the group survived the pop-out): it keeps its own shield
		return;
	}
	setAcceptTouchEvents(false);
	if (!m_closing)
	{
		setVisible(false);
	}
	update();
}

//protected Q_SLOTS:
void GroupOverlay::slotInputSwallowSafetyTimeout()
{
	endInputSwallow();
}

void GroupOverlay::computeAnimationOrigin()
{
	//grow out of the group icon the user tapped, when it can be located
	m_originICS = m_panelRect.center();
	if (m_qp_groupIcon && scene() && (m_qp_groupIcon->scene() == scene()))
	{
		m_originICS = mapFromScene(m_qp_groupIcon->mapToScene(QPointF(0.0,0.0)));
	}
}

void GroupOverlay::applyOpenProgressToIcons()
{
	qreal scale = animationScale();
	for (int i = 0;i < m_shownIcons.size();++i)
	{
		if ((!m_shownIcons[i]) || (m_shownIcons[i]->parentItem() != this))
		{
			continue;
		}
		m_shownIcons[i]->setOpacity(m_openProgress);
		m_shownIcons[i]->setScale(scale);
		m_shownIcons[i]->setPos(m_originICS
								+ (m_iconRects[i].center() - m_originICS) * scale);
	}
}

void GroupOverlay::relayoutMembers()
{
	releaseShownIcons();
	if (!m_qp_groupIcon)
	{
		return;
	}

	QList<IconBase *> memberList = m_qp_groupIcon->members();
	int shownCount = qMin(memberList.size(),(int)GROUP_OVERLAY_MAX_SHOWN);
	int columns = qMin(shownCount,(int)GROUP_OVERLAY_MAX_COLUMNS);
	if (columns < 1)
	{
		columns = 1;
	}
	int rows = (shownCount + columns - 1) / columns;
	if (rows < 1)
	{
		rows = 1;
	}

	//size cells from the icons' real geometry (icon art + label) so they don't overlap
	qreal cellEdge = GROUP_OVERLAY_CELL_EDGE_PX;
	for (int i = 0;i < shownCount;++i)
	{
		if (memberList[i])
		{
			QRectF memberGeom = memberList[i]->geometry();
			cellEdge = qMax(cellEdge,(qreal)(qMax(memberGeom.width(),memberGeom.height()) + 8.0));
		}
	}

	qreal panelW = columns * cellEdge + 2 * GROUP_OVERLAY_PADDING_PX;
	qreal panelH = rows * cellEdge + GROUP_OVERLAY_TITLE_HEIGHT_PX + 2 * GROUP_OVERLAY_PADDING_PX;
	//ICS is centered around 0
	m_panelRect = QRectF(-panelW/2.0,-panelH/2.0,panelW,panelH);
	computeAnimationOrigin();
	m_titleRect = QRectF(m_panelRect.left() + GROUP_OVERLAY_PADDING_PX,
						m_panelRect.top() + GROUP_OVERLAY_PADDING_PX/2,
						panelW - 2*GROUP_OVERLAY_PADDING_PX,
						GROUP_OVERLAY_TITLE_HEIGHT_PX);

	qreal gridLeft = m_panelRect.left() + GROUP_OVERLAY_PADDING_PX;
	qreal gridTop = m_titleRect.bottom();

	for (int i = 0;i < shownCount;++i)
	{
		IconBase * pMemberIcon = memberList[i];
		if (!pMemberIcon)
		{
			continue;
		}
		int col = i % columns;
		int row = i / columns;
		QRectF cellRect(gridLeft + col*cellEdge,
						gridTop + row*cellEdge,
						cellEdge,cellEdge);
		m_shownIcons << QPointer<IconBase>(pMemberIcon);
		m_iconRects << cellRect;
		//members may still carry reorder-mode graphics from when they were dragged in;
		// inside the panel they always show as normal icons
		pMemberIcon->slotChangeRemoveDeleteDecoratorVisibility(RemoveDeleteDecoratorSelector::None);
		pMemberIcon->slotChangeIconFrameVisibility(false);
		pMemberIcon->setParentItem(this);
		pMemberIcon->slotEnableIconAutoRepaint();
		pMemberIcon->clearAutopaintClipRect();
		pMemberIcon->setZValue(zValue()+1.0);
	}
	//place (and scale/fade) the members for wherever the open/close animation is right now
	applyOpenProgressToIcons();
	update();
}

//virtual
bool GroupOverlay::resize(quint32 newWidth,quint32 newHeight)
{
	ThingPaintable::resize(newWidth,newHeight);
	//a panel animating out of a geometry that no longer exists: just end it
	finishClose();
	if (m_showing)
	{
		//re-center the panel (and re-place the member icons) in the new geometry
		bool wasEditing = m_editingName;
		QString savedBuffer = m_editBuffer;
		relayoutMembers();
		if (wasEditing)
		{
			m_editBuffer = savedBuffer;
		}
	}
	update();
	return true;
}

void GroupOverlay::forgetIcon(IconBase * p_icon)
{
	if (!p_icon)
	{
		return;
	}
	for (int i = m_shownIcons.size()-1;i >= 0;--i)
	{
		if (m_shownIcons[i].data() == p_icon)
		{
			//the icon may be mid open/close animation; hand it over unscaled and opaque
			p_icon->setOpacity(1.0);
			p_icon->setScale(1.0);
			m_shownIcons.removeAt(i);
			m_iconRects.removeAt(i);
		}
	}
}

bool GroupOverlay::isEditingName() const
{
	return m_editingName;
}

void GroupOverlay::startNameEdit()
{
	if (m_editingName || (!m_qp_groupIcon))
	{
		return;
	}
	m_editingName = true;
	m_editBuffer = m_qp_groupIcon->groupName();

	raiseKeyboardForEdit();
	//take scene key focus; remember the old holder so type-to-search keeps working
	// after the edit ends
	m_qp_prevFocusItem = 0;
	if (scene() && scene()->focusItem())
	{
		m_qp_prevFocusItem = scene()->focusItem()->toGraphicsObject();
	}
	setFlag(QGraphicsItem::ItemIsFocusable,true);
	setFocus(Qt::OtherFocusReason);
	//and grab the keyboard outright: a stray tap (e.g. one leaking through around the
	// vkb) can move scene focus mid-edit, and the grab keeps keystrokes coming here
	grabKeyboard();

	//lift the PANEL clear of the keyboard - not the whole item, which must keep
	// shielding the entire launcher from touches
	m_panelRect.translate(0.0,-(qreal)GROUP_OVERLAY_EDIT_SHIFT_UP_PX);
	m_titleRect.translate(0.0,-(qreal)GROUP_OVERLAY_EDIT_SHIFT_UP_PX);
	for (int i = 0;i < m_iconRects.size();++i)
	{
		m_iconRects[i].translate(0.0,-(qreal)GROUP_OVERLAY_EDIT_SHIFT_UP_PX);
	}
	m_originICS.setY(m_originICS.y() - (qreal)GROUP_OVERLAY_EDIT_SHIFT_UP_PX);
	applyOpenProgressToIcons();
	update();
}

void GroupOverlay::raiseKeyboardForEdit()
{
	//FieldType_Search keeps the keyboard in plain key-event mode (no trace/candidate
	// machinery, see CandidateBar::setEditorState) - keystrokes arrive as QKeyEvents
	// on the scene focus item, which is this overlay while editing
	PalmIME::EditorState editorState(PalmIME::FieldType_Search);
	::strncpy(editorState.enterKeyLabel,"Done",sizeof(editorState.enterKeyLabel)-1);
	setInputState(editorState);
	setInputFocus(true);
	//setClient() only notifies when the client actually changes, so the explicit
	// notify is what re-opens the keyboard when this panel is already the client
	IMEController::instance()->setClient(this);
	IMEController::instance()->notifyInputFocusChange(this,true);
}

void GroupOverlay::commitNameEdit()
{
	if (!m_editingName)
	{
		return;
	}
	QString newName = m_editBuffer.trimmed();
	if (m_qp_groupIcon && (!newName.isEmpty()) && (newName != m_qp_groupIcon->groupName()))
	{
		m_qp_groupIcon->setGroupName(newName);
		m_qp_groupIcon->update();
		m_titleText = newName;
		if (m_qp_launcher)
		{
			m_qp_launcher->saveCurrentLauncherLayouts();
		}
	}
	endNameEditCommon();
}

void GroupOverlay::cancelNameEdit()
{
	if (!m_editingName)
	{
		return;
	}
	endNameEditCommon();
}

void GroupOverlay::endNameEditCommon()
{
	m_editingName = false;
	m_editBuffer.clear();
	setInputFocus(false);
	IMEController::instance()->removeClient(this);
	ungrabKeyboard();
	if (m_qp_prevFocusItem)
	{
		m_qp_prevFocusItem->setFocus(Qt::OtherFocusReason);
	}
	else
	{
		clearFocus();
	}
	m_qp_prevFocusItem = 0;
	//back to being unfocusable, so later taps on the panel leave scene focus alone
	setFlag(QGraphicsItem::ItemIsFocusable,false);
	//snap the panel and members back to their canonical layout
	if (m_showing)
	{
		relayoutMembers();
	}
	update();
}

//virtual
void GroupOverlay::commitText(const std::string& text)
{
	if (!m_editingName)
	{
		return;
	}
	m_editBuffer += QString::fromUtf8(text.c_str());
	m_editBuffer.truncate(GROUP_OVERLAY_MAX_NAME_LEN);
	update();
}

//virtual
void GroupOverlay::performEditorAction(PalmIME::FieldAction action)
{
	Q_UNUSED(action);
	commitNameEdit();
}

//virtual
void GroupOverlay::removeInputFocus()
{
	//the ime is taking the keyboard away: the vkb's own hide key (SysmgrIMEModel::
	// requestHide -> IMEController::hideIME), a card claiming the ime, the bluetooth
	// keyboard appearing. Cards override this hook; this panel has to as well, or the
	// edit stays open with no keyboard and no way back - tapping the title does nothing
	// while m_editingName is set - and the panel keeps the scene focus and the keyboard
	// grab it took for the edit, which is the focus trap the README warns about.
	InputClient::removeInputFocus();
	commitNameEdit();
}

//virtual
void GroupOverlay::keyPressEvent(QKeyEvent * event)
{
	if (!m_editingName)
	{
		QGraphicsObject::keyPressEvent(event);
		return;
	}
	switch (event->key())
	{
	case Qt::Key_Return:
	case Qt::Key_Enter:
		commitNameEdit();
		break;
	case Qt::Key_Escape:
		cancelNameEdit();
		break;
	case Qt::Key_Backspace:
		m_editBuffer.chop(1);
		update();
		break;
	default:
	{
		QString entered = event->text();
		if ((!entered.isEmpty()) && entered.at(0).isPrint()
			&& (m_editBuffer.length() < GROUP_OVERLAY_MAX_NAME_LEN))
		{
			m_editBuffer += entered;
			update();
		}
		break;
	}
	}
	event->accept();
}

IconBase * GroupOverlay::memberAtPointICS(const QPointF& pointICS) const
{
	for (int i = 0;i < m_iconRects.size();++i)
	{
		if (m_iconRects[i].contains(pointICS))
		{
			return m_shownIcons.value(i,QPointer<IconBase>(0));
		}
	}
	return 0;
}

//virtual
bool GroupOverlay::sceneEvent(QEvent * event)
{
	if ((!m_showing) && (!m_ignoreInputUntilRelease))
	{
		return QGraphicsObject::sceneEvent(event);
	}
	switch (event->type())
	{
	case QEvent::GestureOverride:
	{
		QGestureEvent * ge = static_cast<QGestureEvent*>(event);
		ge->accept();
		return true;
	}
	case QEvent::Gesture:
	{
		if (m_ignoreInputUntilRelease)
		{
			//the finger that popped an icon out is still down: the tap this release makes
			// belongs to that hold, not to whatever the panel was covering
			event->accept();
			return true;
		}
		QGestureEvent * ge = static_cast<QGestureEvent*>(event);
		QGesture * g = ge->gesture(Qt::TapGesture);
		if (g)
		{
			QTapGesture * tap = static_cast<QTapGesture*>(g);
			if (tap->state() == Qt::GestureFinished)
			{
				tapGesture(tap,ge);
			}
			return true;
		}
		g = ge->gesture(Qt::TapAndHoldGesture);
		if (g)
		{
			QTapAndHoldGesture * hold = static_cast<QTapAndHoldGesture*>(g);
			if (hold->state() == Qt::GestureFinished)
			{
				tapAndHoldGesture(hold,ge);
			}
			return true;
		}
		return true;
	}
	//swallow all touches so the page below never sees them while the overlay is up
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
		event->accept();
		return true;
	case QEvent::TouchEnd:
		//the gesture manager filters events ahead of the scene, so the tap this release
		// makes has already come and gone by now: safe to let go of the shield here
		endInputSwallow();
		event->accept();
		return true;
	default:
		break;
	}
	return QGraphicsObject::sceneEvent(event);
}

//virtual
bool GroupOverlay::tapGesture(QTapGesture * tapEvent,QGestureEvent * baseGestureEvent)
{
	QPointF tapICS = mapFromScene(baseGestureEvent->mapToGraphicsScene(tapEvent->hotSpot()));
	if (m_editingName)
	{
		//while editing, any tap off the title just commits the name
		if (!m_titleRect.contains(tapICS))
		{
			commitNameEdit();
		}
		else
		{
			//a tap back on the title asks for the keyboard again: it does nothing while
			// the keyboard is already up, and gets it back if anything took it away
			raiseKeyboardForEdit();
		}
		return true;
	}
	if (!m_panelRect.contains(tapICS))
	{
		hideOverlay();
		return true;
	}
	if (m_titleRect.contains(tapICS))
	{
		startNameEdit();
		return true;
	}
	IconBase * pMemberIcon = memberAtPointICS(tapICS);
	if (pMemberIcon)
	{
		Q_EMIT signalMemberIconTapped(pMemberIcon);
	}
	return true;
}

//virtual
bool GroupOverlay::tapAndHoldGesture(QTapAndHoldGesture * tapHoldEvent,QGestureEvent * baseGestureEvent)
{
	if (m_editingName)
	{
		return true;
	}
	QPointF tapICS = mapFromScene(baseGestureEvent->mapToGraphicsScene(tapHoldEvent->hotSpot()));
	IconBase * pMemberIcon = memberAtPointICS(tapICS);
	if (pMemberIcon)
	{
		//the pop-out happens with the finger still down, and it can move the panel out from
		// under it (the panel shrinks, or dissolves outright with the group): swallow input
		// until that touch ends, so the release can't tap through to what's now underneath
		m_ignoreInputUntilRelease = true;
		QTimer::singleShot(GROUP_OVERLAY_SWALLOW_SAFETY_MS,this,SLOT(slotInputSwallowSafetyTimeout()));
		Q_EMIT signalMemberIconPoppedOut(pMemberIcon);
	}
	return true;
}

//virtual
void GroupOverlay::paint(QPainter * painter,const QStyleOptionGraphicsItem * option,QWidget * widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	if ((!m_showing) && (!m_closing))
	{
		return;
	}
	//dim the launcher behind the panel; the dim fades with the panel
	painter->fillRect(m_geom,QColor(0,0,0,(int)(120.0 * m_openProgress)));

	painter->setRenderHint(QPainter::Antialiasing);

	//the panel scales out of (and back into) the group icon, fading as it goes
	qreal scale = animationScale();
	painter->save();
	painter->translate(m_originICS);
	painter->scale(scale,scale);
	painter->translate(-m_originICS);
	painter->setOpacity(painter->opacity() * m_openProgress);

	painter->setPen(QPen(QColor(255,255,255,90),1.5));
	painter->setBrush(QColor(25,25,25,235));
	painter->drawRoundedRect(m_panelRect,12.0,12.0);

	painter->setFont(m_titleFont);
	if (m_editingName)
	{
		//edit box around the title, with a simple trailing-bar cursor
		painter->setPen(QPen(QColor(140,180,255,220),1.5));
		painter->setBrush(QColor(0,0,0,180));
		painter->drawRoundedRect(m_titleRect.adjusted(4.0,4.0,-4.0,-4.0),6.0,6.0);
		painter->setPen(QColor(255,255,255,235));
		painter->drawText(m_titleRect,Qt::AlignCenter,m_editBuffer + QChar('|'));
	}
	else if (!m_titleText.isEmpty())
	{
		painter->setPen(QColor(255,255,255,235));
		painter->drawText(m_titleRect,Qt::AlignCenter,m_titleText);
	}

	painter->restore();
}

//virtual
void GroupOverlay::paintOffscreen(QPainter * painter)
{
	Q_UNUSED(painter);
}
