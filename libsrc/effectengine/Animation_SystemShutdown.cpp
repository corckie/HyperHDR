#include <effectengine/Animation_SystemShutdown.h>

Animation_SystemShutdown::Animation_SystemShutdown() :
	AnimationBase(ANIM_SYSTEM_SHUTDOWN)
{
	speed = 1.2 * 0.5;
	alarm_color = { 255, 0, 0 };
	post_color = {255, 174, 11};
	shutdown_enabled = false;
	initial_blink = true,
	set_post_color = true;
	initial_blink_index = 0;
	yIndex = SYSTEMSHUTDOWN_HEIGHT;
};

EffectDefinition Animation_SystemShutdown::getDefinition()
{
	EffectDefinition ed;
	ed.name = ANIM_SYSTEM_SHUTDOWN;
	ed.args = GetArgs();
	return ed;
}

void Animation_SystemShutdown::Init(
	QImage& hyperImage,
	int hyperLatchTime	
)
{
	
	hyperImage = hyperImage.scaled(SYSTEMSHUTDOWN_WIDTH, SYSTEMSHUTDOWN_HEIGHT);	

	SetSleepTime(int(round(speed * 1000.0)));
}

void Animation_SystemShutdown::setLine(QPainter* painter, int y, Point3d rgb)
{
	painter->setPen(QColor().fromRgb(rgb.x, rgb.y, rgb.z));
	painter->drawLine(0, y, SYSTEMSHUTDOWN_WIDTH, y);
}

void Animation_SystemShutdown::setFill(QPainter* painter, Point3d rgb)
{
	painter->fillRect(0, 0, SYSTEMSHUTDOWN_WIDTH, SYSTEMSHUTDOWN_HEIGHT, QColor().fromRgb(rgb.x, rgb.y, rgb.z));
}

bool Animation_SystemShutdown::Play(QPainter* painter)
{
	bool ret = true;

	if (initial_blink)
	{
		if (initial_blink_index < 6)
		{			
			if (initial_blink_index++ % 2)
				setFill(painter, alarm_color);
			else
				setFill(painter, Point3d() = { 0,0,0 });
			return ret;
		}
		else
		{
			setFill(painter, Point3d() = { 0,0,0 });
			initial_blink = false;
			return ret;
		}
	}

	if (yIndex-- >= 0)
	{				
		setLine(painter, yIndex, alarm_color);
		return ret;
	}

	if (yIndex-- == -1)
	{
		SetSleepTime(int(round(1000.0)));
		return ret;
	}	

	if (set_post_color)
	{
		setFill(painter, post_color);
		SetSleepTime(int(round(2000.0)));
		set_post_color = false;
	}
	else
		setStopMe(true);

	return ret;
}

QJsonObject Animation_SystemShutdown::GetArgs() {
	QJsonObject doc;
	doc["smoothing-custom-settings"] = false;
	return doc;
}





