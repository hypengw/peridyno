#include "GlfwGUI/GlfwApp.h"

#include "Framework/SceneGraph.h"
#include "Framework/Log.h"

#include "Peridynamics/ParticleElasticBody.h"
#include "Peridynamics/Cloth.h"

#include "ParticleSystem/StaticBoundary.h"

#include "module/PointRender.h"
#include "module/SurfaceRender.h"


using namespace std;
using namespace dyno;

void CreateScene()
{
	SceneGraph& scene = SceneGraph::getInstance();

	std::shared_ptr<StaticBoundary<DataType3f>> root = scene.createNewScene<StaticBoundary<DataType3f>>();
	root->loadCube(Vec3f(0), Vec3f(1), 0.005f, true);
	root->loadShpere(Vec3f(0.5, 0.7f, 0.5), 0.08f, 0.005f, false, true);

	std::shared_ptr<Cloth<DataType3f>> cloth = std::make_shared<Cloth<DataType3f>>();
	cloth->setMass(1.0);
	cloth->loadParticles("../../data/cloth/cloth.obj");
	cloth->loadSurface("../../data/cloth/cloth.obj");

	root->addParticleSystem(cloth);

	auto pointRenderer = std::make_shared<PointRenderer>();
	pointRenderer->setColor(glm::vec3(1, 0.2, 1));
	cloth->addVisualModule(pointRenderer);
	cloth->setVisible(true);

	auto surfaceRenderer = std::make_shared<SurfaceRenderer>();
	cloth->getSurface()->addVisualModule(surfaceRenderer);
}

int main()
{
	CreateScene();

	GlfwApp window;
	window.setCameraType(CameraType::TrackBall);
	window.createWindow(1024, 768);
	window.mainLoop();
	return 0;
}