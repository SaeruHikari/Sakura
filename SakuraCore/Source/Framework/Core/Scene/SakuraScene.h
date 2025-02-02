/*****************************************************************************************
								 Copyrights   SaeruHikari
CreateDate:								2019.10.30
Description:	  Sakura scene class, contains all scene nodes and managers.
Details:
*******************************************************************************************/
#pragma once
#include "Framework/Interface/ISObject.h"
#include "../Nodes/SakuraSceneNode.hpp"

namespace SScene
{
	class SakuraScene : public SakuraSceneNode
	{
	public:
		SakuraScene()
			:SakuraSceneNode()
		{
			NodeName = "Scene";
		}
		// Implements base ISTickObject interfaces.
		virtual bool Initialize() override;
		virtual void Finalize() override;
		virtual void Tick(double deltaTime) override;
		void CreateTestSphereMatrix();
		// Add Entity to the entities root.
		virtual bool AddSceneNode(SakuraSceneNode* childNode);
		// Add Entity to the entities root.
		virtual bool AddSceneNode(std::unique_ptr<SakuraSceneNode> childNode);
		// Find scene node
		virtual SakuraSceneNode* FindSceneNode(SGuid id);

		__forceinline SakuraSceneNode* GetRoot() { return EntitiesRoot.get(); }
	private:
		std::unique_ptr<SakuraSceneNode> EntitiesRoot;
	};
}
