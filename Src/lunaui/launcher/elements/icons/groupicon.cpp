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

#include "groupicon.h"
#include "pixmapobject.h"
#include "dimensionsglobal.h"
#include "icongeometrysettings.h"
#include "dimensionslauncher.h"
#include "appmonitor.h"
#include "webosapp.h"

#include <QPainter>
#include <QImage>
#include <QFont>
#include <QPen>
#include <QColor>
#include <QLinearGradient>

//size of the composite pixmap; slightly larger than the 64px app main icons so the
// group tile reads as a container (the icon geometry box is 128px, plenty of room)
#define GROUP_COMPOSITE_EDGE_PX		68
//size of one member thumbnail within the composite
#define GROUP_COMPOSITE_SUB_EDGE_PX	26
//inner padding between the backplate edge and the thumbnails
#define GROUP_COMPOSITE_MARGIN_PX	6

//static
GroupIcon * GroupIcon::groupIconFromPix(PixmapObject * p_framePix,PixmapObject * p_mainPix,
										PixmapObject * p_feedbackPix,Page * p_belongsTo)
{
	if (p_mainPix == 0)
	{
		return 0;
	}
	//same geometry determination as IconBase::iconFromPix
	QRectF geom = IconBase::GEOM(QRectF());
	if (!geom.isEmpty())
	{
		return new GroupIcon(geom,p_framePix,p_mainPix,p_feedbackPix,p_belongsTo);
	}
	if (!p_framePix)
	{
		return 0;
	}
	QRectF inferredGeom = DimensionsGlobal::realRectAroundRealPoint(
			QSize((1.0/IconGeometrySettings::settings()->frameBoxProportionToGeom.width())*p_framePix->width(),
					(1.0/IconGeometrySettings::settings()->frameBoxProportionToGeom.height())*p_framePix->height())
			);
	return new GroupIcon(inferredGeom,p_framePix,p_mainPix,p_feedbackPix,p_belongsTo);
}

GroupIcon::GroupIcon(const QRectF& iconGeometry,PixmapObject * p_framePix,PixmapObject * p_mainPix,
					PixmapObject * p_feedbackPix,Page * p_belongsTo)
: IconBase(iconGeometry,p_framePix,p_mainPix,p_feedbackPix,p_belongsTo)
, m_qp_compositePmo(p_mainPix)
{
}

//virtual
GroupIcon::~GroupIcon()
{
	if (m_qp_compositePmo)
	{
		delete m_qp_compositePmo;
	}
}

//virtual
IconBase * GroupIcon::clone()
{
	return 0;
}

QString GroupIcon::groupName() const
{
	return iconLabel();
}

void GroupIcon::setGroupName(const QString& name)
{
	setIconLabel(name);
}

QList<IconBase *> GroupIcon::members() const
{
	QList<IconBase *> rlist;
	for (QList<QPointer<IconBase> >::const_iterator it = m_memberIcons.constBegin();
			it != m_memberIcons.constEnd();++it)
	{
		if (*it)
		{
			rlist << (*it).data();
		}
	}
	return rlist;
}

int GroupIcon::memberCount() const
{
	return members().size();
}

bool GroupIcon::containsMember(IconBase * p_icon) const
{
	if (!p_icon)
	{
		return false;
	}
	IconBase * pMaster = p_icon->master();
	for (QList<QPointer<IconBase> >::const_iterator it = m_memberIcons.constBegin();
			it != m_memberIcons.constEnd();++it)
	{
		if ((*it) && ((*it).data() == pMaster))
		{
			return true;
		}
	}
	return false;
}

bool GroupIcon::addMember(IconBase * p_icon)
{
	if (!p_icon)
	{
		return false;
	}
	IconBase * pMaster = p_icon->master();
	if ((pMaster == this) || containsMember(pMaster))
	{
		return false;
	}
	m_memberIcons << QPointer<IconBase>(pMaster);
	refreshCompositePixmap();
	return true;
}

bool GroupIcon::removeMember(IconBase * p_icon)
{
	if (!p_icon)
	{
		return false;
	}
	IconBase * pMaster = p_icon->master();
	bool removed = false;
	for (int i = m_memberIcons.size()-1;i >= 0;--i)
	{
		if (!m_memberIcons[i])
		{
			//prune dead entries while here
			m_memberIcons.removeAt(i);
		}
		else if (m_memberIcons[i].data() == pMaster)
		{
			m_memberIcons.removeAt(i);
			removed = true;
		}
	}
	if (removed)
	{
		refreshCompositePixmap();
	}
	return removed;
}

void GroupIcon::refreshCompositePixmap()
{
	PixmapObject * pNewPmo = renderCompositePmo(members());
	PixmapObject * pOldPmo = 0;
	slotUpdateIconPic(pNewPmo,true,pOldPmo);
	if (mainPixmap() != pNewPmo)
	{
		//the icon rejected the new pixmap (size check); keep what we have and toss the new one
		delete pNewPmo;
		return;
	}
	//only dispose of pixmaps this class created itself
	if (pOldPmo && (pOldPmo == m_qp_compositePmo.data()))
	{
		delete pOldPmo;
	}
	m_qp_compositePmo = pNewPmo;
}

//loads the member's icon art fresh from its file. Reading back the member's live
// pixmap (which may be GPU-resident by now) returns garbage or stale textures on
// device, so the file is the only reliable source at runtime.
static QImage memberArtImage(IconBase * p_memberIcon)
{
	if (!p_memberIcon)
	{
		return QImage();
	}
	LauncherObject * pLauncher = LauncherObject::primaryInstance();
	if (pLauncher)
	{
		QUuid appUid = pLauncher->appUidFromIconUid(p_memberIcon->master()->uid());
		DimensionsSystemInterface::WebOSApp * pApp =
				qobject_cast<DimensionsSystemInterface::WebOSApp *>(
						DimensionsSystemInterface::AppMonitor::appMonitor()->appByUid(appUid));
		if (pApp)
		{
			QImage artImg(pApp->mainIconFilePath());
			if (!artImg.isNull())
			{
				return artImg.convertToFormat(QImage::Format_ARGB32_Premultiplied);
			}
		}
	}
	//fallback: readback from the live pixmap
	PixmapObject * pPmo = p_memberIcon->mainPixmap();
	if (pPmo && pPmo->data() && (!pPmo->data()->isNull()))
	{
		return pPmo->data()->toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}
	return QImage();
}

//static
PixmapObject * GroupIcon::renderCompositePmo(const QList<IconBase *>& memberIcons)
{
	QSize size(GROUP_COMPOSITE_EDGE_PX,GROUP_COMPOSITE_EDGE_PX);
	PixmapObject * pPmo = new PixmapObject(size.width(),size.height());
	//paint into an ARGB image first: device pixmaps may be 16bpp without an alpha
	// channel, which would turn the transparent area into an opaque square
	QImage compositeImg(size,QImage::Format_ARGB32_Premultiplied);
	compositeImg.fill(0);
	QPainter painter(&compositeImg);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);

	//backplate, so the group reads as a container even when nearly empty.
	// a soft drop shadow + vertical gradient + thicker border give it a little depth
	QRectF plateRect(1.0,1.0,size.width()-2.0,size.height()-3.0);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(0,0,0,120));
	painter.drawRoundedRect(plateRect.translated(0.0,1.5),8.0,8.0);
	QLinearGradient plateGradient(plateRect.topLeft(),plateRect.bottomLeft());
	plateGradient.setColorAt(0.0,QColor(111,111,116,160));
	plateGradient.setColorAt(0.35,QColor(61,61,66,150));
	plateGradient.setColorAt(1.0,QColor(36,36,38,170));
	painter.setPen(QPen(QColor(255,255,255,95),2.0));
	painter.setBrush(plateGradient);
	painter.drawRoundedRect(plateRect,8.0,8.0);

	const int sub = GROUP_COMPOSITE_SUB_EDGE_PX;
	const int margin = GROUP_COMPOSITE_MARGIN_PX;
	const int gap = size.width() - 2*sub - 2*margin;

	int slotsUsed = qMin(memberIcons.size(),4);
	bool overflow = (memberIcons.size() > 4);
	for (int i = 0;i < slotsUsed;++i)
	{
		int x = margin + (i % 2) * (sub + gap);
		int y = margin + (i / 2) * (sub + gap);
		if (overflow && (i == 3))
		{
			//an overflow marker instead of the 4th thumbnail
			painter.setPen(QColor(255,255,255,220));
			QFont f = painter.font();
			f.setPixelSize(14);
			f.setBold(true);
			painter.setFont(f);
			painter.drawText(QRect(x,y,sub,sub),Qt::AlignCenter,
					QString("+%1").arg(memberIcons.size()-3));
			continue;
		}
		IconBase * pMemberIcon = memberIcons[i];
		if (!pMemberIcon)
		{
			continue;
		}
		QImage memberImg = memberArtImage(pMemberIcon);
		if (!memberImg.isNull())
		{
			painter.drawImage(QRect(x,y,sub,sub),memberImg);
		}
	}
	painter.end();
	*(pPmo->data()) = QPixmap::fromImage(compositeImg);
	return pPmo;
}
