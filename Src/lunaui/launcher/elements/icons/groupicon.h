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

#ifndef GROUPICON_H_
#define GROUPICON_H_

#include "icon.h"

#include <QList>
#include <QPointer>
#include <QString>
#include <QPixmap>

class PixmapObject;
class Page;

/*
 * GroupIcon - an icon that is a named container of other (app) icons; the launcher's
 * "app group"/"folder" concept.
 *
 * It occupies a single IconCell like any other icon. Its main pixmap is a composite
 * mini-grid rendered from the member icons' main pixmaps, regenerated whenever
 * membership changes; because only the main pixmap differs from a regular icon,
 * every existing paint/clip/offscreen path works on it unmodified.
 *
 * The group name is simply the icon label (see IconBase iconlabel property).
 * Member icons are always stored as master icons. The group does NOT own the member
 * icons (IconHeap does) and must never become their QGraphicsItem parent.
 */
class GroupIcon : public IconBase
{
	Q_OBJECT

public:

	// mirrors IconBase::iconFromPix
	static GroupIcon * groupIconFromPix(PixmapObject * p_framePix,PixmapObject * p_mainPix,
										PixmapObject * p_feedbackPix,Page * p_belongsTo);

	GroupIcon(const QRectF& iconGeometry,PixmapObject * p_framePix,PixmapObject * p_mainPix,
				PixmapObject * p_feedbackPix,Page * p_belongsTo);
	virtual ~GroupIcon();

	//groups cannot be cloned, so they can never land on the quicklaunch bar;
	// IconHeap::copyIcon and the QL transfer path both null-check (see handleQuickLaunchSpecialMoveArea)
	virtual IconBase * clone();

	QString groupName() const;
	void setGroupName(const QString& name);

	//pruned of any icons that have gone away
	QList<IconBase *> members() const;
	int memberCount() const;
	bool containsMember(IconBase * p_icon) const;

	//adds the icon's master at the end of the member list.
	// does NOT touch location tags or the icon's scene/parent state; callers handle that
	bool addMember(IconBase * p_icon);
	bool removeMember(IconBase * p_icon);

	//re-render the mini-grid composite into this icon's main pixmap
	void refreshCompositePixmap();

	//makes a new composite pixmap object from the member icons' main pixmaps; caller owns it
	static PixmapObject * renderCompositePmo(const QList<IconBase *>& memberIcons);

private:

	QList<QPointer<IconBase> > m_memberIcons;
	QPointer<PixmapObject> m_qp_compositePmo;	//the current main pixmap; owned by this class
};

#endif /* GROUPICON_H_ */
