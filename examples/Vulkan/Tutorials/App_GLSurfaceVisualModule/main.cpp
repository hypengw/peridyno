#include <GlfwApp.h>

#include <SceneGraph.h>
#include <GLSurfaceVisualModule.h>

#include "Cloth.h"

using namespace dyno;

std::shared_ptr<SceneGraph> createScene()
{
	auto scene = std::make_shared<SceneGraph>();

	auto cloth = scene->addNode(std::make_shared<Cloth>());

	cloth->loadObjFile(getAssetPath() + "/bunny/sparse_bunny_mesh.obj");

	auto clothRender = std::make_shared<dyno::GLSurfaceVisualModule>();
	cloth->stateTopology()->connect(clothRender->inTriangleSet());
	cloth->graphicsPipeline()->pushModule(clothRender);

	return scene;
}

int main(int, char**)
{
	VkSystem::instance()->setAssetPath(getAssetPath());
	VkSystem::instance()->initialize();

	GlfwApp window;
	window.initialize(1024, 768);
	window.setSceneGraph(createScene());
	window.mainLoop();
	return 0;
}
