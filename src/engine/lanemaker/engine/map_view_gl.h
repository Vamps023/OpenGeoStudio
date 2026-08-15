#pragma once

#include "Math.hpp"
#include "Camera.h"
#include "gl_buffer_manage.h"
#include "action_defs.h"
#include "touch_controller.h"

#include <QOpenGLWidget>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <qvector2d.h>
#include <QMatrix4x4>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <optional>
#include <map>
#include <vector>

namespace LM
{
	const unsigned int NVehicleVariations = 3;

	class AbstractGraphicsItem;
	class PermanentGraphics;
	class InstancedGraphics;
	class UILayover;

	class MapViewGL : public QOpenGLWidget, QOpenGLExtraFunctions
	{
		friend class AbstractGraphicsItem;
		friend class PermanentGraphics;
		friend class InstancedGraphics;
		friend class UILayover;

		friend class FreeRotController;
		friend class TouchController;

		Q_OBJECT
	public:
		MapViewGL();
		void CleanupResources();
		void ResetCamera();

		// 2D/3D view mode
		enum class ViewMode { Perspective3D, TopDown2D };
		void SetViewMode(ViewMode mode);
		ViewMode GetViewMode() const { return m_viewMode; }

		// Map background — dynamic tile-based satellite imagery
		void SetMapCenter(double lat, double lon);
		void ClearMapBackground();
		void UpdateMapTiles(); // called on pan/zoom to load visible tiles

		// permanent objects only
		void UpdateObject(unsigned int objectID, uint8_t flag);
		void UpdateObjectID(unsigned int graphicsID, unsigned int objectID);
		uint8_t GetObjectFlag(unsigned int objectID);
		void RemoveObject(unsigned int objectID);

		void SetViewFromReplay(Transform3D t);
		void UpdateRayHit(QPoint screen, bool fromReplay=false);
		int VBufferUseage_pct() const;
		float Zoom() const;

		// Zoom controls (for toolbar buttons)
		void ZoomIn();
		void ZoomOut();

	signals:
		void MousePerformedAction(LM::MouseAction); // excluding view adjustment / Scene button event
        void KeyPerformedAction(LM::KeyPressAction);// excluding view adjustment, including Scene button event

	protected:
		void initializeGL() override;
		void resizeGL(int width, int height) override;
		void paintGL() override;

		// Returns graphics ID. If objectID is -1, it is considered temporary.
		unsigned int AddQuads(const odr::Line3D& lBorder, const odr::Line3D& rBorder, QColor color, unsigned int objID = -1);
		unsigned int AddLine(const odr::Line3D& border, double width, QColor color, unsigned int objID = -1);
		unsigned int AddPoly(const odr::Line3D& boundary, QColor color, unsigned int objID = -1);
		unsigned int AddColumn(const odr::Line3D& boundary, double h, QColor color, unsigned int objID = -1);
		void RemoveItem(unsigned int graphicsID, bool temporary = false);

		// Instanced rendering, for traffic.
		void AddInstance(unsigned int id, QColor color, unsigned int variation);
		void UpdateInstance(unsigned int, const QMatrix4x4, unsigned int);
		void RemoveInstance(unsigned int, unsigned int);

		// Background
		unsigned int AddBackgroundLine(const odr::Line3D& line, double width, QColor color);
		void RemoveBackground(unsigned int);

		// Layover
		void AddSceneLayover(uint32_t id, odr::Vec3D scenePos, QPixmap icon, QRect lwOffset, int syntax);
		void RemoveSceneLayover(uint32_t id);

		void mousePressEvent(QMouseEvent* event) override;
		void mouseDoubleClickEvent(QMouseEvent* event) override;
		void mouseMoveEvent(QMouseEvent* event) override;
		void mouseReleaseEvent(QMouseEvent* event) override;
		void wheelEvent(QWheelEvent* event) override;

		bool event(QEvent* event) override;
		
#ifdef __linux__
		public:
#endif
		void keyPressEvent(QKeyEvent* event) override;

	private:
		static void LineToQuads(const odr::Line3D& border, double width, odr::Line3D& outLSide, odr::Line3D& outRSide);

		QPoint lastMousePos;
		bool dragPan = false;
		std::optional<FreeRotController> freeRotateSession;
		std::optional<TouchController> touchSession;
		std::optional<int> touchSessionType;

		// Blender-style 3D viewport controls
		enum class BlenderNavMode { None, Orbit, Pan };
		BlenderNavMode m_blenderNav = BlenderNavMode::None;
		QPoint m_blenderLastPos;
		void blenderOrbit(QPoint delta);
		void blenderPan(QPoint delta);

		QVector3D PointerDirection(QPoint cursor) const;

		QPointF PixelLocation(QVector3D globalDir) const;

		QVector2D PointerOnGround(QPoint cursor) const;

		std::unique_ptr<GLBufferManage>     permanentBuffer;
		std::unique_ptr<GLBufferManage>     temporaryBuffer;
		std::unique_ptr<GLBufferManage>     backgroundBuffer;
		std::array<GLBufferManageInstanced, NVehicleVariations> vehicleBuffer;

		QMatrix4x4					m_worldToView;	// cached world to view transformation matrix

		/*! The projection matrix, updated whenever the viewport geometry changes (in resizeGL() ). */
		QMatrix4x4					m_projection;
		Camera						m_camera;		// Camera position, orientation and lens data

		struct SceneTiedLayover
		{
			uint32_t id;
			odr::Vec3D pos;
			QRect lwOffset;
			QPixmap icon;
			int syntax;

			QRect renderedRect(QMatrix4x4 worldToView) const;
		};
		std::map<uint32_t, SceneTiedLayover> sceneTiedLayovers;

        bool ignoreNextMouseRelease; // for scene buttons
        bool m_painting = false; // recursion guard for paintGL

        // 2D/3D view mode
        ViewMode m_viewMode = ViewMode::Perspective3D;

        // Dynamic tile cache for satellite map
        // QGIS-inspired approach: download tiles as QImage, composite into
        // a single image, upload as one OpenGL texture, draw as one quad.
        struct MapTile {
            int z, x, y; // tile coordinates
            double worldX, worldY; // center in world meters
            double worldSize; // size in world meters
            bool loading = false;
            QImage image; // downloaded tile image
        };
        std::vector<std::unique_ptr<MapTile>> m_mapTiles;
        double m_mapCenterLat = 18.52;
        double m_mapCenterLon = 73.85;
        int m_mapZoom = 16;
        bool m_mapEnabled = false;
        QNetworkAccessManager* m_tileNam = nullptr;
        int m_lastTileZoom = -1;
        double m_lastCameraX = 0, m_lastCameraY = 0, m_lastCameraZ = 0;

        // Single composite texture for the entire map (QGIS-style)
        std::unique_ptr<QOpenGLTexture> m_mapTexture;
        QImage m_mapCompositeImage;
        bool m_mapCompositeDirty = true;
        double m_mapCompositeWorldMinX = 0, m_mapCompositeWorldMinY = 0;
        double m_mapCompositeWorldMaxX = 0, m_mapCompositeWorldMaxY = 0;

        // Shader for textured quad
        QOpenGLShaderProgram m_texturedShader;
        QOpenGLBuffer m_bgQuadVbo;
        QOpenGLVertexArrayObject m_bgQuadVao;
        bool m_texturedShaderInit = false;

        void initTexturedShader();
        void drawMapBackground();
        void requestTile(int z, int x, int y);
        void pruneInvisibleTiles();
        void compositeMapImage();
        static void latLonToTile(double lat, double lon, int z, int& tx, int& ty);
        static void tileToLatLon(int tx, int ty, int z, double& lat, double& lon);
        static double metersPerPixel(double lat, int z);
	};

	extern MapViewGL* g_mapViewGL;
	extern std::string g_PointerRoadID;
	extern double g_PointerRoadS;
	extern int g_PointerLane;
	extern odr::Vec2D g_PointerOnGround;
	extern odr::Vec3D g_CameraPosition;
	extern int g_createRoadElevationOption;
	extern unsigned int g_PointerVehicle;
	extern bool touchScreen;
}