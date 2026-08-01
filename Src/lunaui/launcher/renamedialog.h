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

#ifndef RENAMEDIALOG_H_
#define RENAMEDIALOG_H_

#include "thingpaintable.h"
#include "InputClient.h"

#include <QPointer>
#include <QRectF>
#include <QFont>
#include <QString>

class LauncherObject;
class QTapGesture;
class QTapAndHoldGesture;
class QGestureEvent;
class QKeyEvent;

/*
 * RenameDialog - a small modal text-entry panel over the launcher, driven by the
 * system virtual keyboard. Used for renaming launcher tabs (and reusable for any
 * other single-line rename).
 *
 * Uses the same input recipe as the group overlay's inline rename: it registers
 * as the IME's InputClient (raising the keyboard), takes scene key focus AND
 * grabs the keyboard (so a stray tap can't divert keystrokes), and shields the
 * whole launcher from touches while up.
 */
class RenameDialog : public ThingPaintable , public InputClient
{
	Q_OBJECT

public:

	RenameDialog(const QRectF& geometry,LauncherObject * p_launcher);
	virtual ~RenameDialog();

	//promptText is the small heading (e.g. "Rename Tab"); currentText primes the field
	void showDialog(const QString& promptText,const QString& currentText);
	//commit=true emits signalRenameCommitted with the edited text first
	void hideDialog(bool commit);
	bool isShowing() const;

	// InputClient - virtual keyboard hookup
	virtual void commitText(const std::string& text);
	virtual void performEditorAction(PalmIME::FieldAction action);

Q_SIGNALS:

	void signalRenameCommitted(const QString& newText);
	void signalDismissed();

protected:

	virtual bool sceneEvent(QEvent * event);
	virtual bool tapGesture(QTapGesture * tapEvent,QGestureEvent * baseGestureEvent);
	virtual void keyPressEvent(QKeyEvent * event);

	virtual void paint(QPainter * painter,const QStyleOptionGraphicsItem * option=0,QWidget * widget=0);
	virtual void paintOffscreen(QPainter * painter);

private:

	QPointer<LauncherObject> m_qp_launcher;
	bool m_showing;
	QString m_promptText;
	QString m_editBuffer;
	QRectF m_panelRect;		//ICS
	QRectF m_promptRect;	//ICS
	QRectF m_editRect;		//ICS
	QFont m_promptFont;
	QFont m_editFont;
	QPointer<QGraphicsObject> m_qp_prevFocusItem;
};

#endif /* RENAMEDIALOG_H_ */
