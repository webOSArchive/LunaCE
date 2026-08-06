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
class PixmapObject;
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

	//promptText is the small heading (e.g. "Rename Tab"); currentText primes the field.
	// commitOnTapAway: tapping outside the panel keeps the edit (rename) rather than
	// throwing it away (create). showDeleteButton adds the trash affordance.
	void showDialog(const QString& promptText,const QString& currentText,
					bool commitOnTapAway = true,bool showDeleteButton = false);
	//commit=true emits signalRenameCommitted with the edited text first.
	// notifyOwner=false tears down silently (used when one dialog replaces another),
	// so the owner's idea of what the dialog is acting on isn't cleared underneath it
	void hideDialog(bool commit,bool notifyOwner = true);
	bool isShowing() const;

	//keeps the panel centered when the launcher is resized (e.g. rotation)
	virtual bool resize(quint32 newWidth,quint32 newHeight);

	// InputClient - virtual keyboard hookup
	virtual void commitText(const std::string& text);
	virtual void performEditorAction(PalmIME::FieldAction action);
	//the ime taking the keyboard away closes the dialog; see the note on the group
	// overlay's override for what a dialog left open without one costs
	virtual void removeInputFocus();

Q_SIGNALS:

	void signalRenameCommitted(const QString& newText);
	void signalDeleteRequested();
	void signalDismissed();

protected:

	virtual bool sceneEvent(QEvent * event);
	virtual bool tapGesture(QTapGesture * tapEvent,QGestureEvent * baseGestureEvent);
	virtual void keyPressEvent(QKeyEvent * event);

	virtual void paint(QPainter * painter,const QStyleOptionGraphicsItem * option=0,QWidget * widget=0);
	virtual void paintOffscreen(QPainter * painter);

private:

	//(re)computes m_panelRect/m_promptRect/m_editRect/m_deleteRect for the current geom
	void recalculateLayout();

	QPointer<LauncherObject> m_qp_launcher;
	bool m_showing;
	bool m_commitOnTapAway;
	bool m_showDeleteButton;
	//set when the trash was tapped; the delete is signalled during teardown, before
	// signalDismissed, so the owner still knows which page the dialog was acting on
	bool m_deleteRequested;
	QString m_promptText;
	QString m_editBuffer;
	QRectF m_panelRect;		//ICS
	QRectF m_promptRect;	//ICS
	QRectF m_editRect;		//ICS
	QRectF m_deleteRect;	//ICS
	QFont m_promptFont;
	QFont m_editFont;
	QPointer<PixmapObject> m_qp_deleteIconPmo;
	QPointer<QGraphicsObject> m_qp_prevFocusItem;
};

#endif /* RENAMEDIALOG_H_ */
