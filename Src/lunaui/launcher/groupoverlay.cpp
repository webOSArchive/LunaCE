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

GroupOverlay::GroupOverlay(const QRectF& geometry,LauncherObject * p_launcher)
: ThingPaintable(geometry)
, m_qp_launcher(p_launcher)
, m_qp_groupIcon(0)
, m_qp_ownerPage(0)
, m_showing(false)
, m_editingName(false)
{
	m_titleFont = QFont(QString("Prelude"));
	m_titleFont.setPixelSize(22);
	m_titleFont.setBold(true);

	setAcceptTouchEvents(true);
	grabGesture(Qt::TapGesture);
	grabGesture(Qt::TapAndHoldGesture);
	setFlag(QGraphicsItem::ItemIsFocusable,true);
	setZValue(100.0);
	setVisible(false);
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
	m_qp_groupIcon = p_groupIcon;
	m_qp_ownerPage = p_ownerPage;
	m_showing = true;
	relayoutMembers();
	setVisible(true);
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
	releaseShownIcons();
	m_qp_groupIcon = 0;
	m_qp_ownerPage = 0;
	m_showing = false;
	setVisible(false);
	Q_EMIT signalDismissed();
}

void GroupOverlay::releaseShownIcons()
{
	for (QList<QPointer<IconBase> >::iterator it = m_shownIcons.begin();
			it != m_shownIcons.end();++it)
	{
		if (*it)
		{
			(*it)->slotDisableIconAutoRepaint();
			(*it)->setParentItem(0);
			if ((*it)->scene())
			{
				(*it)->scene()->removeItem(*it);
			}
		}
	}
	m_shownIcons.clear();
	m_iconRects.clear();
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
		pMemberIcon->setPos(cellRect.center());
		pMemberIcon->setZValue(zValue()+1.0);
	}
	update();
}

//virtual
bool GroupOverlay::resize(quint32 newWidth,quint32 newHeight)
{
	ThingPaintable::resize(newWidth,newHeight);
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

	//FieldType_Search keeps the keyboard in plain key-event mode (no trace/candidate
	// machinery, see CandidateBar::setEditorState) - keystrokes arrive as QKeyEvents
	// on the scene focus item, which is this overlay while editing
	PalmIME::EditorState editorState(PalmIME::FieldType_Search);
	::strncpy(editorState.enterKeyLabel,"Done",sizeof(editorState.enterKeyLabel)-1);
	setInputState(editorState);
	setInputFocus(true);
	IMEController::instance()->setClient(this);
	IMEController::instance()->notifyInputFocusChange(this,true);
	//take scene key focus; remember the old holder so type-to-search keeps working
	// after the edit ends
	m_qp_prevFocusItem = 0;
	if (scene() && scene()->focusItem())
	{
		m_qp_prevFocusItem = scene()->focusItem()->toGraphicsObject();
	}
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
		if (m_shownIcons[i])
		{
			m_shownIcons[i]->setPos(m_iconRects[i].center());
		}
	}
	update();
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
	if (!m_showing)
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
	case QEvent::TouchEnd:
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
		Q_EMIT signalMemberIconPoppedOut(pMemberIcon);
	}
	return true;
}

//virtual
void GroupOverlay::paint(QPainter * painter,const QStyleOptionGraphicsItem * option,QWidget * widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	if (!m_showing)
	{
		return;
	}
	//dim the launcher behind the panel
	painter->fillRect(m_geom,QColor(0,0,0,120));

	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(QPen(QColor(255,255,255,90),1.5));
	painter->setBrush(QColor(25,25,25,235));
	painter->drawRoundedRect(m_panelRect,12.0,12.0);

	if (m_qp_groupIcon)
	{
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
		else
		{
			painter->setPen(QColor(255,255,255,235));
			painter->drawText(m_titleRect,Qt::AlignCenter,m_qp_groupIcon->groupName());
		}
	}
}

//virtual
void GroupOverlay::paintOffscreen(QPainter * painter)
{
	Q_UNUSED(painter);
}
