#ifndef vis_object_dataH
#define vis_object_dataH
#pragma once

// Структура, хранящие уникальные данные для конкретного визуала --#SM+#--
// PS: Неоптимально хранить эти данные для каждой модели (и её составных), но ради удобства приходится жертвовать немного ОЗУ
struct	vis_object_data
{
public:
	// == Weapons == //
	int			m_max_bullet_bones;	// Максимальное число костей в модели с привязкой к числу патронов (0 по дефолту)

	// == HUD == //
	float		m_hud_custom_fov;	// Кастомный FOV для рендера этой модели в режиме худа (-1.f по дефолту)

	// == Generic == //
	Fmatrix     sh_camo_data;       // Данные для камуфляжа (передаются в шейдеры)
	Fvector4    sh_custom_data;     // Кастомные данные, содержимое которых зависит от использующих их шейдеров и кода (передаются в шейдеры)
	Fvector4    sh_entity_data;     // Параметры "живых" объектов (передаются в шейдеры)
			/*
				1) health		- здоровье объекта (-2 если такого параметра нет у объекта)
				2) radiation	- радиация объекта (-2 если такого параметра нет у объекта)
				3) condition	- кондишион объекта (-2 если такого параметра нет у объекта)
				4) irnv value	- коэфицент "теплового излучения" модели (0.0 - 1.0)
			*/

	// Инициализируем начальные данные
	vis_object_data()
	{
		m_max_bullet_bones	= 0;
		m_hud_custom_fov	= -1.f;

		sh_camo_data		= Fmatrix();
		sh_custom_data.set	(0.f,	0.f,	0.f,	0.f);
		sh_entity_data.set	(-2.f,	-2.f,	-2.f,	0.f);
	}
};

#endif	// vis_object_dataH
