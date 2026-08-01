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

#include "renamedialog.h"
#include "dimensionslauncher.h"
#include "IMEController.h"

#include <QPainter>
#include <QPen>
#include <QColor>
#include <QEvent>
#include <QGesture>
#include <QGestureEvent>
#include <QGraphicsScene>
#include <QKeyEvent>

#include <cstring>

#define RENAME_DIALOG_PANEL_WIDTH_PX	520
#define RENAME_DIALOG_PANEL_HEIGHT_PX	150
#define RENAME_DIALOG_PROMPT_HEIGHT_PX	44
#define RENAME_DIALOG_PADDING_PX		18
//the panel sits above screen center so the virtual keyboard can't cover it
#define RENAME_DIALOG_SHIFT_UP_PX		140
#define RENAME_DIALOG_MAX_TEXT_LEN		24

RenameDialog::RenameDialog(const QRectF& geometry,LauncherObject * p_launcher)
: ThingPaintable(geometry)
, m_qp_launcher(p_launcher)
, m_showing(false)
{
	m_promptFont = QFont(QString("Prelude"));
	m_promptFont.setPixelSize(18);
	m_editFont = QFont(QString("Prelude"));
	m_editFont.setPixelSize(24);
	m_editFont.setBold(true);

	m_panelRect = QRectF(-RENAME_DIALOG_PANEL_WIDTH_PX/2.0,
						-RENAME_DIALOG_PANEL_HEIGHT_PX/2.0 - RENAME_DIALOG_SHIFT_UP_PX,
						RENAME_DIALOG_PANEL_WIDTH_PX,RENAME_DIALOG_PANEL_HEIGHT_PX);
	m_promptRect = QRectF(m_panelRect.left() + RENAME_DIALOG_PADDING_PX,
						m_panelRect.top() + RENAME_DIALOG_PADDING_PX/2,
						m_panelRect.width() - 2*RENAME_DIALOG_PADDING_PX,
						RENAME_DIALOG_PROMPT_HEIGHT_PX);
	m_editRect = QRectF(m_panelRect.left() + RENAME_DIALOG_PADDING_PX,
						m_promptRect.bottom(),
						m_panelRect.width() - 2*RENAME_DIALOG_PADDING_PX,
						m_panelRect.bottom() - m_promptRect.bottom() - RENAME_DIALOG_PADDING_PX);

	setAcceptTouchEvents(true);
	grabGesture(Qt::TapGesture);
	grabGesture(Qt::TapAndHoldGesture);
	setFlag(QGraphicsItem::ItemIsFocusable,true);
	setZValue(110.0);
	setVisible(false);
}

//virtual
RenameDialog::~RenameDialog()
{
	if (m_showing)
	{
		IMEController::instance()->removeClient(this);
	}
}

bool RenameDialog::isShowing() const
{
	return m_showing;
}

void RenameDialog::showDialog(const QString& promptText,const QString& currentText)
{
	if (m_showing)
	{
		hideDialog(false);
	}
	m_showing = true;
	m_promptText = promptText;
	m_editBuffer = currentText;
	m_editBuffer.truncate(RENAME_DIALOG_MAX_TEXT_LEN);

	//same input recipe as the group overlay rename (see groupoverlay.cpp)
	PalmIME::EditorState editorState(PalmIME::FieldType_Search);
	::strncpy(editorState.enterKeyLabel,"Done",sizeof(editorState.enterKeyLabel)-1);
	setInputState(editorState);
	setInputFocus(true);
	IMEController::instance()->setClient(this);
	IMEController::instance()->notifyInputFocusChange(this,true);
	m_qp_prevFocusItem = 0;
	if (scene() && scene()->focusItem())
	{
		m_qp_prevFocusItem = scene()->focusItem()->toGraphicsObject();
	}
	setFocus(Qt::OtherFocusReason);
	grabKeyboard();

	setVisible(true);
	update();
}

void RenameDialog::hideDialog(bool commit)
{
	if (!m_showing)
	{
		return;
	}
	QString finalText = m_editBuffer.trimmed();

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
	m_showing = false;
	m_editBuffer.clear();
	setVisible(false);

	if (commit && (!finalText.isEmpty()))
	{
		Q_EMIT signalRenameCommitted(finalText);
	}
	Q_EMIT signalDismissed();
}

//virtual
void RenameDialog::commitText(const std::string& text)
{
	if (!m_showing)
	{
		return;
	}
	m_editBuffer += QString::fromUtf8(text.c_str());
	m_editBuffer.truncate(RENAME_DIALOG_MAX_TEXT_LEN);
	update();
}

//virtual
void RenameDialog::performEditorAction(PalmIME::FieldAction action)
{
	Q_UNUSED(action);
	hideDialog(true);
}

//virtual
bool RenameDialog::sceneEvent(QEvent * event)
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
		}
		return true;
	}
	//swallow all touches so nothing below reacts while the dialog is up
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
bool RenameDialog::tapGesture(QTapGesture * tapEvent,QGestureEvent * baseGestureEvent)
{
	QPointF tapICS = mapFromScene(baseGestureEvent->mapToGraphicsScene(tapEvent->hotSpot()));
	if (!m_panelRect.contains(tapICS))
	{
		//tapping away commits, same behavior as the group rename
		hideDialog(true);
	}
	return true;
}

//virtual
void RenameDialog::keyPressEvent(QKeyEvent * event)
{
	if (!m_showing)
	{
		QGraphicsObject::keyPressEvent(event);
		return;
	}
	switch (event->key())
	{
	case Qt::Key_Return:
	case Qt::Key_Enter:
		hideDialog(true);
		break;
	case Qt::Key_Escape:
		hideDialog(false);
		break;
	case Qt::Key_Backspace:
		m_editBuffer.chop(1);
		update();
		break;
	default:
	{
		QString entered = event->text();
		if ((!entered.isEmpty()) && entered.at(0).isPrint()
			&& (m_editBuffer.length() < RENAME_DIALOG_MAX_TEXT_LEN))
		{
			m_editBuffer += entered;
			update();
		}
		break;
	}
	}
	event->accept();
}

//virtual
void RenameDialog::paint(QPainter * painter,const QStyleOptionGraphicsItem * option,QWidget * widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	if (!m_showing)
	{
		return;
	}
	//dim everything behind the panel
	painter->fillRect(m_geom,QColor(0,0,0,120));

	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(QPen(QColor(255,255,255,90),1.5));
	painter->setBrush(QColor(25,25,25,235));
	painter->drawRoundedRect(m_panelRect,12.0,12.0);

	painter->setPen(QColor(255,255,255,170));
	painter->setFont(m_promptFont);
	painter->drawText(m_promptRect,Qt::AlignCenter,m_promptText);

	//edit box with a simple trailing-bar cursor
	painter->setPen(QPen(QColor(140,180,255,220),1.5));
	painter->setBrush(QColor(0,0,0,180));
	painter->drawRoundedRect(m_editRect.adjusted(4.0,4.0,-4.0,-4.0),6.0,6.0);
	painter->setPen(QColor(255,255,255,235));
	painter->setFont(m_editFont);
	painter->drawText(m_editRect,Qt::AlignCenter,m_editBuffer + QChar('|'));
}

//virtual
void RenameDialog::paintOffscreen(QPainter * painter)
{
	Q_UNUSED(painter);
}
