#include <vk_engine.h>

/* Remarks list for now:
* - Need to take a better look over barriers (stage and access) and implement it better for our purposes
* - Need to check out image layout so we can specify better current and new image layouts 
*/

int main(int argc, char* argv[])
{
	VulkanEngine engine;

	engine.Init();	
	
	engine.Run();	

	engine.Cleanup();	

	return 0;
}
