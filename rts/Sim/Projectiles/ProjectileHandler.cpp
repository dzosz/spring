/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <algorithm>
#include <sstream>

#include "Projectile.h"
#include "ProjectileHandler.h"
#include "ProjectileMemPool.h"
#include "Game/GlobalUnsynced.h"
#include "Game/TraceRay.h"
#include "Map/Ground.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GroundFlash.h"
#include "Sim/Features/Feature.h"
#include "Sim/Features/FeatureDef.h"
#include "Sim/Misc/CollisionHandler.h"
#include "Sim/Misc/CollisionVolume.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/Misc/TeamHandler.h"
#include "Rendering/Env/Particles/Classes/NanoProjectile.h"
#include "Sim/Projectiles/WeaponProjectiles/WeaponProjectile.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Weapons/WeaponDef.h"
#include "Sim/Weapons/PlasmaRepulser.h"
#include "System/Config/ConfigHandler.h"
#include "System/EventHandler.h"
#include "System/Log/ILog.h"
#include "System/Cpp11Compat.hpp"
#include "System/SpringMath.h"
#include "System/TimeProfiler.h"
#include "System/Threading/ThreadPool.h"
#include "Rendering/Env/Particles/ProjectileDrawer.h"

#include "lib/entt/src/entt/entt.hpp"
#include "Rendering/Env/Particles/ECS_systems.h"
#include "Rendering/Env/Particles/Classes/SimpleParticleSystem.h"
#include "Rendering/Env/Particles/Classes/BitmapMuzzleFlame.h"
#include "Rendering/Env/Particles/Classes/DirtProjectile.h"
#include "Rendering/Env/Particles/Classes/ExploSpikeProjectile.h"
#include "Rendering/Env/Particles/Classes/HeatCloudProjectile.h"
#include "Rendering/Env/Particles/Classes/MuzzleFlame.h"
#include "Rendering/Env/Particles/Classes/SmokeProjectile.h"
#include "Rendering/Env/Particles/Classes/SmokeTrailProjectile.h"
#include "Rendering/Env/Particles/Classes/BubbleProjectile.h"

#include <typeindex>

// reserve 5% of maxNanoParticles for important stuff such as capture and reclaim other teams' units
#define NORMAL_NANO_PRIO 0.95f
#define HIGH_NANO_PRIO 1.0f


CONFIG(int, MaxParticles).defaultValue(10000).headlessValue(0).minimumValue(0);
CONFIG(int, MaxNanoParticles).defaultValue(2000).headlessValue(0).minimumValue(0);

bool ECS_MODE = false; // runtime switch to enable/disable ECS to see graphical differences

CR_BIND(CProjectileHandler, )
CR_REG_METADATA(CProjectileHandler, (
	CR_MEMBER(projectiles),
	CR_MEMBER_UN(flyingPieces),
	CR_MEMBER_UN(groundFlashes),
	CR_MEMBER_UN(resortFlyingPieces),

	CR_MEMBER(maxParticles),
	CR_MEMBER(maxNanoParticles),
	CR_MEMBER(currentNanoParticles),
	CR_MEMBER_UN(frameCurrentParticles),
	CR_MEMBER_UN(frameProjectileCounts)
))



// note: stores all ExpGenSpawnable types, not just projectiles
ProjMemPool projMemPool;

CProjectileHandler projectileHandler;

entt::registry projectileRegistry;
static entt::organizer ecsTaskList;
static std::vector<CProjectile*> queuedProjectiles; 
static std::unordered_map<std::type_index, std::function<void(CProjectile*)>> ecsSpawner;

bool isEcsProj(const CProjectile* pro) {
//	if (dynamic_cast<const CSimpleParticleSystem*>(pro)) {
//		return true;
//	}
	if (!ECS_MODE)
		return false;
	auto type_idx = std::type_index(typeid(*pro));
	auto it = ecsSpawner.find(type_idx);
	return (it != ecsSpawner.end());
}

namespace {

static void createECSTaskGraph() {
	ecsTaskList.clear();	
	ecsTaskList.emplace<&UpdateSimpleParticleSystem>();
	ecsTaskList.emplace<&UpdateBitmapMuzzleFlame>();
	ecsTaskList.emplace<&UpdateDirtProjectile>();
	ecsTaskList.emplace<&UpdateHeatCloudProjectile>();
	ecsTaskList.emplace<&UpdateSmokeProjectile>();
	ecsTaskList.emplace<&UpdateSmokeTrailProjectile>();
	ecsTaskList.emplace<&UpdateCMuzzleFlame>();
	ecsTaskList.emplace<&UpdateExploSpikeProjectile>();
	
	ecsTaskList.emplace<&DeleteDestroyedSystem>();
	
	// preallocate pools
	LOG("ECS Task List:");
	int idx = 0;
	for(auto &&node: ecsTaskList.graph()) {
		node.prepare(projectileRegistry);
		auto children = node.children();
		
		std::ostringstream oss;
		std::copy(children.begin(), children.end(), std::ostream_iterator<size_t>(oss, " "));
		LOG("%i %.*s child tasks: %s", idx, static_cast<int>(node.info().name().length()), node.info().name().data(), oss.str().c_str());
		++idx;
	}
}

static void createECSGroups() { // for better iteration performance
	/*
	projectileRegistry.group<const SimpleParticle, const AnimParams2>();
 	projectileRegistry.group<const ParticlePhys>(entt::get<Speed>);
 	projectileRegistry.group<SmokeSized, const SmokeSizeChange>();
 	projectileRegistry.group<LifetimeFlame, const FlameSizeChange>();
	*/
}

static void updateECSParticles() {
	// FIXME we can't execute on threadpool until we move all legacy projectiles into ECS
	ZoneScopedN("XYZ::Sim::Projectiles::Update::ECS");
	projectileRegistry.ctx().at<PhysDelta>().frameNum = gs->frameNum;

	// execute ecs system updates
	auto tasks = ecsTaskList.graph();
	for (auto& vert : tasks) {
		vert.callback()(vert.data(), projectileRegistry);
	}


	/* // MT EXECUTION
	{
		for_mt_chunk(0, tasks.size()-1, [&tasks](int i) {
			auto& vert = tasks[i];
			vert.callback()(vert.data(), projectileRegistry);
		}, 1);
	}
	
	auto vert = tasks.back();
	vert.callback()(vert.data(), projectileRegistry);
	*/
}

} // unnamed namespace

void CProjectileHandler::AddECSProjectile(CSimpleParticleSystem* proj) {
	//TracyPlot("drawOrdSPS", (float)proj->drawOrder);
	for (int i=0; i< proj->GetProjectilesCount(); ++i)
	{
		auto ent = projectileRegistry.create();
		auto& p = proj->particles[i];
		projectileRegistry.emplace<SimpleParticle>(ent,
			p.pos, p.speed, proj->gravity, proj->airdrag,
			p.rotVal, p.rotVel, proj->rotParams,
			p.life, p.decayrate, p.size,
			proj->sizeGrowth, proj->sizeMod,
			proj->allyteamID, proj->castShadow, proj->useAirLos,
			proj->drawPos, proj->drawRadius,
			DrawOrder{proj->drawOrder, 0.0f},
			RenderData{proj->texture, nullptr, proj->colorMap, proj->directional},
			proj->animProgress, proj->animParams, proj->createFrame,		
			false, false, false, false,
			std::array<unsigned char, 4>{}	
		);
	}
}

void CProjectileHandler::AddECSProjectile(CBitmapMuzzleFlame* p) {
	auto ent = projectileRegistry.create();
	
	projectileRegistry.emplace<BitmapMuzzleFlame>(ent,
		p->pos, p->speed, p->dir,
		p->size,
		p->length, p->sizeGrowth,
		p->frontOffset,
		p->ttl, p->invttl,
		p->rotVal, p->rotVel, p->rotParams,
		p->allyteamID, p->castShadow, p->useAirLos,
		false, false, false, false,
		p->drawPos, p->drawRadius, DrawOrder{p->drawOrder, 0.0f},
		RenderData{p->frontTexture, p->sideTexture, p->colorMap, false},
		p->animProgress, p->animParams, p->createFrame
	);
}

void CProjectileHandler::AddECSProjectile(CDirtProjectile* proj) {
	auto ent = projectileRegistry.create();

	projectileRegistry.emplace<DirtProjectile>(ent,
	    proj->alpha, proj->alphaFalloff,
	    proj->size, proj->sizeExpansion,
	    proj->mygravity, proj->slowdown, proj->color,
		proj->allyteamID, proj->castShadow, proj->useAirLos,
		false, false, false, false,							   
	    proj->pos, proj->speed,
		proj->drawPos, proj->drawRadius, DrawOrder{proj->drawOrder, 0.0f},
		RenderData{proj->texture, nullptr, nullptr, false},
		proj->animProgress, proj->animParams, proj->createFrame
	);
}

void CProjectileHandler::AddECSProjectile(CExploSpikeProjectile* p) {
	auto ent = projectileRegistry.create();

	projectileRegistry.emplace<ExploSpikeProjectile>(
		ent,
		p->length, p->width,
		p->alpha, p->alphaDecay,
		p->lengthGrowth,
		p->color, 
		p->allyteamID, p->castShadow, p->useAirLos,
		false, false, false, false,
		p->pos, p->speed, p->dir,
		p->drawPos, p->drawRadius, DrawOrder{p->drawOrder, 0.0f},
		RenderData{nullptr, nullptr, nullptr, false},
		p->animProgress, p->animParams, p->createFrame
	);
}

void CProjectileHandler::AddECSProjectile(CHeatCloudProjectile* p)
{
	auto ent = projectileRegistry.create();
	
	projectileRegistry.emplace<HeatCloudProjectile>(
		ent,
		p->heat, p->maxheat, p->heatFalloff,
		p->size, p->sizeGrowth, p->sizemod, p->sizemodmod,
		p->pos, p->speed,
		p->rotVal, p->rotVel, p->rotParams,
		p->allyteamID, p->castShadow, p->useAirLos,
		false, false, false, false,
		p->drawPos, p->drawRadius, DrawOrder{p->drawOrder, 0.0f},
		RenderData{p->texture, nullptr, nullptr, false},
		p->animProgress, p->animParams, p->createFrame					   
	);
}


void CProjectileHandler::AddECSProjectile(CMuzzleFlame* p)
{
	auto ent = projectileRegistry.create();
	for (int i =0; i < p->numSmoke; ++i) {
		auto texture = projectileDrawer->GetSmokeTexture(i % projectileDrawer->NumSmokeTextures());
				
	 	projectileRegistry.emplace<MuzzleFlame>(ent,
			p->size, p->age,
			p->numFlame, p->numSmoke,
			i, p->randSmokeDir[i],
			p->pos, p->speed, p->dir,
			p->allyteamID, p->castShadow, p->useAirLos,
			false, false, false, false,
			p->drawPos, p->drawRadius, DrawOrder{p->drawOrder, 0.0f},
			RenderData{texture, nullptr, nullptr, false},
			p->animProgress, p->animParams, p->createFrame						
		);
	}
}

void CProjectileHandler::AddECSProjectile(CSmokeProjectile* proj)
{
	auto ent = projectileRegistry.create();

	projectileRegistry.emplace<SmokeProjectile>(ent,
		proj->color, proj->age, proj->ageSpeed,
		proj->size, proj->startSize, proj->sizeExpansion,
		proj->pos, proj->speed,
		proj->allyteamID, proj->castShadow, proj->useAirLos,
		false, false, false, false,
		proj->drawPos, proj->drawRadius, DrawOrder{proj->drawOrder, 0.0f},
		RenderData{projectileDrawer->GetSmokeTexture(proj->textureNum), nullptr, nullptr, false},
		proj->animProgress, proj->animParams, proj->createFrame
	);
}

void CProjectileHandler::AddECSProjectile(CSmokeTrailProjectile* proj)
{
	//auto ent = projectileRegistry.create();	
	auto ent = entt::entity(proj->ent); // FIXME temporary workaround required because of UpdateEndPos() external calls
	
	if (!projectileRegistry.valid(ent)) {
		throw 130;
	}
	
	projectileRegistry.emplace<SmokeTrail>(ent,
		proj->pos1,
		proj->pos2,
		proj->origSize,
		proj->creationTime, proj->lifeTime, proj->lifePeriod,
		proj->color, proj->dir1, proj->dir2,
		proj->dirpos1, proj->dirpos2,
		proj->midpos, proj->middir,	   
		proj->drawSegmented, proj->firstSegment, proj->lastSegment,
		proj->allyteamID, proj->castShadow, proj->useAirLos,
		false, false, false, false,
		proj->pos, proj->speed,
		proj->drawPos, proj->drawRadius, DrawOrder{proj->drawOrder, 0.0f},
		RenderData{proj->texture, nullptr, nullptr, false},
		proj->animProgress, proj->animParams, proj->createFrame
	);
}

void CProjectileHandler::AddECSProjectile(CBubbleProjectile* proj) {
	auto ent = projectileRegistry.create();
	projectileRegistry.emplace<BubbleProjectile>(
				ent,
				proj->ttl, proj->alpha,
				proj->size, proj->startSize,
				proj->sizeExpansion,
				proj->pos, proj->speed,
				proj->allyteamID, proj->castShadow, proj->useAirLos,
				false, false, false, false,
				proj->drawPos, proj->drawRadius, DrawOrder{proj->drawOrder, 0.0f},
				RenderData{nullptr, nullptr, nullptr, false},
				proj->animProgress, proj->animParams, proj->createFrame
				
				);
}

// provides safety to projectiles container.
// It avoids duplicated iteration over projectiles[synced] containers
// and gives control when exactly to Update() new particles
void CProjectileHandler::AddUnsyncedParticleToQueue(CProjectile* proj) {
	// called from st or multithreaded context, already locks Projectile::mut
	queuedProjectiles.push_back(proj);
}

void CProjectileHandler::DrainUnsyncedProjectileQueue() {
	for (auto* p : queuedProjectiles) {
		auto type_idx = std::type_index(typeid(*p));
		auto it = ecsSpawner.find(type_idx);
		if (ECS_MODE && it != ecsSpawner.end()) {
			it->second(p); // calls CProjectileHandler::AddECSProjectile(proj)
			
			// Comment out lines below so we can pause the game and see same frame with Legacy or ECS projectiles
			// when ECS_MODE option is changed
			projMemPool.free(p);
			continue;
		}
		// legacy unsynced projectile path
		p->id = static_cast<int>(projectiles[false].Add(p));
		CreateProjectile(p);
	}
	queuedProjectiles.clear();
}

void CProjectileHandler::Init()
{
	currentNanoParticles = 0;
	frameCurrentParticles = 0;
	frameProjectileCounts[false] = 0;
	frameProjectileCounts[ true] = 0;

	resortFlyingPieces.fill(false);

	maxParticles     = configHandler->GetInt("MaxParticles");
	maxNanoParticles = configHandler->GetInt("MaxNanoParticles");

	projMemPool.clear();
	projMemPool.reserve(1024);

	for (int modelType = 0; modelType < MODELTYPE_CNT; ++modelType) {
		flyingPieces[modelType].clear();
		flyingPieces[modelType].reserve(1000);
	}

	projectiles[true ].SeedFreeKeys(0, 1 << 14, true); //seed only synced free ids.
	projectiles[false].reserve(static_cast<size_t>(maxParticles) * 2);

	CExpGenSpawnable::InitSpawnables();

	// register ConfigNotify()
	configHandler->NotifyOnChange(this, {"MaxParticles", "MaxNanoParticles"});
    
 	projectileRegistry.ctx().emplace<PhysDelta>();
	ConfigNotify({}, {});
	
	ecsSpawner[std::type_index(typeid(CSimpleParticleSystem))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CSimpleParticleSystem*>(p));
	};	

	/*
	ecsSpawner[std::type_index(typeid(CBitmapMuzzleFlame))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CBitmapMuzzleFlame*>(p));
	};

	ecsSpawner[std::type_index(typeid(CHeatCloudProjectile))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CHeatCloudProjectile*>(p));
	};

	ecsSpawner[std::type_index(typeid(CDirtProjectile))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CDirtProjectile*>(p));
	};

	ecsSpawner[std::type_index(typeid(CMuzzleFlame))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CMuzzleFlame*>(p));
	};
	
	ecsSpawner[std::type_index(typeid(CExploSpikeProjectile))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CExploSpikeProjectile*>(p));
	};
	

	ecsSpawner[std::type_index(typeid(CSmokeProjectile))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CSmokeProjectile*>(p));
	};
	/*
	ecsSpawner[std::type_index(typeid(CSmokeTrailProjectile))] = [&](CProjectile* p) {
		AddECSProjectile(static_cast<CSmokeTrailProjectile*>(p));
	};
	*/

	createECSGroups();
	createECSTaskGraph();
	
	TracyPlotConfig("drawOrdSPS", tracy::PlotFormatType::Number, true, false, tracy::Color::Aqua);	
}

void CProjectileHandler::Kill()
{
	configHandler->RemoveObserver(this);

	{
		// synced first, to avoid callback crashes
		for (CProjectile* p: projectiles[true])
			projMemPool.free(p);

		projectiles[true].clear();
	}

	{
		for (CProjectile* p: projectiles[false])
			projMemPool.free(p);

		projectiles[false].clear();
	}

	{
		for (CGroundFlash* gf: groundFlashes)
			projMemPool.free(gf);

		groundFlashes.clear();
	}

	{
		for (auto& fpc: flyingPieces) {
			fpc.clear();
		}
	}
 	projectileRegistry.clear();

	CCollisionHandler::PrintStats();
}


void CProjectileHandler::ConfigNotify(const std::string& key, const std::string& value)
{
	maxParticles     = configHandler->GetInt("MaxParticles");
	maxNanoParticles = configHandler->GetInt("MaxNanoParticles");

	ECS_MODE = maxParticles % 2;
	LOG("ECS MODE = %b ECS particles %ld alive", ECS_MODE, projectileRegistry.alive());

	projectiles[false].reserve(static_cast<size_t>(maxParticles) * 2);
}


static void MAPPOS_SANITY_CHECK(const float3 v)
{
	v.AssertNaNs();
	assert(v.x >= -(float3::maxxpos * 16.0f));
	assert(v.x <=  (float3::maxxpos * 16.0f));
	assert(v.z >= -(float3::maxzpos * 16.0f));
	assert(v.z <=  (float3::maxzpos * 16.0f));
	assert(v.y >= -MAX_PROJECTILE_HEIGHT);
	assert(v.y <=  MAX_PROJECTILE_HEIGHT);
}

template<bool synced>
void CProjectileHandler::UpdateProjectilesImpl()
{
	SCOPED_TIMER("Sim::Projectiles::Update");

	auto& pc = projectiles[synced];
	// WARNING:
	//   we can't use iterators here because ProjectileCreated
	//   and ProjectileDestroyed events may add new projectiles
	//   to the container!
	for (size_t i = 0; i < pc.size(); /*no-op*/) {
		CProjectile* p = pc[i];

		assert(p != nullptr);
		assert(p->synced == synced);
#ifdef USING_CREG
		assert(p->synced == !!(p->GetClass()->flags & creg::CF_Synced));
#endif

		// (delayed) creation for projectiles added after CheckCollisions()
		if (p->createMe)
			CreateProjectile(p);

		// deletion (FIXME: move outside of loop)
		if (p->deleteMe) {
			DestroyProjectile(p);
			continue;
		}

		// neither
		++i;
	}

	// WARNING: same as above but for p->Update()
	if constexpr (synced) {
		for (size_t i = 0; i < pc.size(); ++i) {
			CProjectile* p = pc[i];
			assert(p != nullptr);

			MAPPOS_SANITY_CHECK(p->pos);

			p->Update();
			quadField.MovedProjectile(p);

			MAPPOS_SANITY_CHECK(p->pos);
		}
	}
	else {
		DrainUnsyncedProjectileQueue();
		auto ecs_process_future = std::async(std::launch::async, updateECSParticles);
		//auto ecs_process_future = ThreadPool::Enqueue(updateECSParticles);
		
		auto sps_future = std::async(std::launch::async, [&](){ simpleParticleSystem.Update(); });
		
		//if (ECS_MODE)
		{
			//updateECSParticles();
			//UpdateECSParticlesMT();
			//return;
		}
		
		{
			ZoneScopedN("XYZ::Sim::Projectiles::Update::for_mt_chunk");
			for_mt_chunk(0, pc.size(), [&pc](int i) {
			//for (int i =0; i < pc.size(); ++i) {
				CProjectile* p = pc[i];
				assert(p != nullptr);
	
				MAPPOS_SANITY_CHECK(p->pos);
				p->Update();
				MAPPOS_SANITY_CHECK(p->pos);
			});
		}
		sps_future.wait();
		ecs_process_future.wait();
	}
}


template<class T>
static void UPDATE_PTR_CONTAINER(T& cont) {
	if (cont.empty())
		return;

#ifndef NDEBUG
	const size_t origSize = cont.size();
#endif
	size_t size = cont.size();

	for (size_t i = 0; i < size; /*no-op*/) {
		CGroundFlash*& gf = cont[i];

		if (!gf->Update()) {
			projMemPool.free(gf);
			gf = cont[size -= 1];
			continue;
		}

		++i;
	}

	// WARNING:
	//   check if the vector was enlarged while iterating, in
	//   which case we will have missed updating newest items
	assert(cont.size() == origSize);

	cont.erase(cont.begin() + size, cont.end());
}

template<class T>
static void UPDATE_REF_CONTAINER(T& cont) {
	if (cont.empty())
		return;

#ifndef NDEBUG
	const size_t origSize = cont.size();
#endif
	size_t size = cont.size();

	for (size_t i = 0; i < size; /*no-op*/) {
		auto& p = cont[i];

		if (!p.Update()) {
			p = std::move(cont[size -= 1]);
			continue;
		}

		++i;
	}

	// WARNING: see UPDATE_PTR_CONTAINER
	assert(cont.size() == origSize);

	cont.erase(cont.begin() + size, cont.end());
}



void CProjectileHandler::CreateProjectile(CProjectile* p)
{
	p->createMe = false;

	if (p->synced || PH_UNSYNCED_PROJECTILE_EVENTS == 1)
		eventHandler.ProjectileCreated(p, p->GetAllyteamID());

	eventHandler.RenderProjectileCreated(p);
}

void CProjectileHandler::DestroyProjectile(CProjectile* p)
{
	assert(!p->createMe);

	eventHandler.RenderProjectileDestroyed(p);

	if (p->synced) {
		eventHandler.ProjectileDestroyed(p, p->GetAllyteamID());

		projectiles[true].Del(p->id);

		ASSERT_SYNCED(p->pos);
		ASSERT_SYNCED(p->id);
	} else {
	#if (PH_UNSYNCED_PROJECTILE_EVENTS == 1)
		eventHandler.ProjectileDestroyed(p, p->GetAllyteamID());
	#endif
		projectiles[false].Del(p->id);
	}

	projMemPool.free(p);
}

uint32_t CProjectileHandler::UnsyncedRandInt(uint32_t N) { return guRNG.NextInt(N); }
uint32_t CProjectileHandler::SyncedRandInt  (uint32_t N) { return gsRNG.NextInt(N); }

void CProjectileHandler::Update()
{
	{
		SCOPED_TIMER("Sim::Projectiles");

		// check if any projectiles have collided since the previous update
		CheckCollisions();
		UpdateProjectiles();

		UPDATE_PTR_CONTAINER(groundFlashes);

		// flying pieces; sort these every now and then
		for (int modelType = 0; modelType < MODELTYPE_CNT; ++modelType) {
			auto& fpc = flyingPieces[modelType];

			UPDATE_REF_CONTAINER(fpc);

			if (resortFlyingPieces[modelType]) {
				std::stable_sort(fpc.begin(), fpc.end());
			}
		}		
	}

	// precache part of particles count calculation that else becomes very heavy
	frameCurrentParticles = 0;

	for (const CProjectile* p: projectiles[ true]) {
		frameCurrentParticles += p->GetProjectilesCount();
	}
	for (const CProjectile* p: projectiles[false]) {
		frameCurrentParticles += p->GetProjectilesCount();
	}

	frameProjectileCounts[ true] = projectiles[ true].size();
	frameProjectileCounts[false] = projectiles[false].size();

	// prints currently allocated projectiles every second
    /*
	if (gs->frameNum % 30 == 0) {
		std::map<std::type_index, std::pair<std::string, size_t>> unsynced;
		std::map<std::type_index, std::pair<std::string,size_t>> synced;
				
		for (const CProjectile* p: projectiles[ true]) {
			frameCurrentParticles += p->GetProjectilesCount();
			auto id = std::type_index(typeid(*p));
			if (synced.find(typeid(*p)) == synced.end()) {
				synced.insert(std::pair{id, std::pair{typeid(*p).name(), 0}});
			}
			synced.at(id).second += 1;
		}
				
		for (const CProjectile* p: projectiles[false]) {
			auto id = std::type_index(typeid(*p));
			if (unsynced.find(typeid(*p)) == unsynced.end()) {
				unsynced.insert(std::pair{id, std::pair{typeid(*p).name(), 0}});
			}
			unsynced.at(id).second += 1;
		}
		
		
		for (auto& t : unsynced) {
			LOG("u %s %lu", t.second.first.c_str(), t.second.second);
		}
		for (auto& t : synced) {
			LOG("s %s %lu", t.second.first.c_str(), t.second.second);
		}
	}
	*/
}

void CProjectileHandler::AddProjectile(CProjectile* p)
{
	// already initialized?
	assert(p->id < 0);
	assert(p->createMe);

	if (!p->synced) {
		AddUnsyncedParticleToQueue(p);
		return;
	}

	if (p->synced)
		p->id = static_cast<int>(projectiles[true ].Add(p, rngFuncs[true]));
	else
		p->id = static_cast<int>(projectiles[false].Add(p)); //don't bother with shuffling unsynced ids 

	if (p->synced) {
		ASSERT_SYNCED(freeIDs.size());
		ASSERT_SYNCED(p->id);
	}

	CreateProjectile(p);
}




static bool CheckProjectileCollisionFlags(const CProjectile* p, const CUnit* u)
{
	const unsigned int collFlags = p->GetCollisionFlags() * p->weapon;

	// only weapon-projectiles can have non-zero flags
	if (collFlags == 0)
		return true;

	// disregard everything else when this bit is set
	// (ground and feature flags are tested elsewhere)
	if ((collFlags & Collision::NONONTARGETS) != 0)
		return (static_cast<const CWeaponProjectile*>(p)->GetTargetObject() == u);

	if ((collFlags & Collision::NOCLOAKED) != 0 && u->IsCloaked())
		return false;
	if ((collFlags & Collision::NONEUTRALS) != 0 && u->IsNeutral())
		return false;

	if ((collFlags & Collision::NOFIREBASES) != 0) {
		const CUnit* owner = p->owner();
		const CUnit* trans = (owner != nullptr)? owner->GetTransporter(): nullptr;

		// check if the unit being collided with is occupied by p's owner
		if (u == trans && trans->unitDef->isFirePlatform)
			return false;
	}

	if (teamHandler.IsValidAllyTeam(p->GetAllyteamID())) {
		const bool noFriendsBit = ((collFlags & Collision::NOFRIENDLIES) != 0);
		const bool noEnemiesBit = ((collFlags & Collision::NOENEMIES   ) != 0);
		const bool friendlyFire = teamHandler.AlliedAllyTeams(p->GetAllyteamID(), u->allyteam);

		if (noFriendsBit && friendlyFire)
			return false;
		if (noEnemiesBit && !friendlyFire)
			return false;
	}

	return true;
}


void CProjectileHandler::CheckUnitCollisions(
	CProjectile* p,
	std::vector<CUnit*>& tempUnits,
	const float3 ppos0,
	const float3 ppos1
) {
	if (!p->checkCol)
		return;

	CollisionQuery cq;

	for (CUnit* unit: tempUnits) {
		assert(unit != nullptr);

		// if this unit fired this projectile, always ignore
		if (unit == p->owner())
			continue;
		if (!unit->HasCollidableStateBit(CSolidObject::CSTATE_BIT_PROJECTILES))
			continue;

		if (!CheckProjectileCollisionFlags(p, unit))
			continue;

		if (CCollisionHandler::DetectHit(unit, unit->GetTransformMatrix(true), ppos0, ppos1, &cq)) {
			if (cq.GetHitPiece() != nullptr)
				unit->SetLastHitPiece(cq.GetHitPiece(), gs->frameNum, p->synced);

			if (!cq.InsideHit()) {
				p->SetPosition(cq.GetHitPos());
				p->Collision(unit);
				p->SetPosition(ppos0);
			} else {
				p->Collision(unit);
			}

			break;
		}
	}
}

void CProjectileHandler::CheckFeatureCollisions(
	CProjectile* p,
	std::vector<CFeature*>& tempFeatures,
	const float3 ppos0,
	const float3 ppos1
) {
	// already collided with unit?
	if (!p->checkCol)
		return;

	if ((p->GetCollisionFlags() & Collision::NOFEATURES) != 0)
		return;

	CollisionQuery cq;

	for (CFeature* feature: tempFeatures) {
		assert(feature != nullptr);

		if (!feature->HasCollidableStateBit(CSolidObject::CSTATE_BIT_PROJECTILES))
			continue;

		if (CCollisionHandler::DetectHit(feature, feature->GetTransformMatrix(true), ppos0, ppos1, &cq)) {
			if (cq.GetHitPiece() != nullptr)
				feature->SetLastHitPiece(cq.GetHitPiece(), gs->frameNum, p->synced);

			if (!cq.InsideHit()) {
				p->SetPosition(cq.GetHitPos());
				p->Collision(feature);
				p->SetPosition(ppos0);
			} else {
				p->Collision(feature);
			}

			break;
		}
	}
}


void CProjectileHandler::CheckShieldCollisions(
	CProjectile* p,
	std::vector<CPlasmaRepulser*>& tempRepulsers,
	const float3 ppos0,
	const float3 ppos1
) {
	if (!p->checkCol)
		return;
	// skip unsynced and non-weapon projectiles
	if (!p->weapon)
		return;

	CWeaponProjectile* wpro = static_cast<CWeaponProjectile*>(p);
	const WeaponDef* wdef = wpro->GetWeaponDef();

	const unsigned int interceptType = wdef->interceptedByShieldType;
	const unsigned int projAllyTeam = p->GetAllyteamID();

	// bail early
	if (interceptType == 0)
		return;

	CollisionQuery cq;

	for (CPlasmaRepulser* repulser: tempRepulsers) {
		assert(repulser != nullptr);

		if (!repulser->CanIntercept(interceptType, projAllyTeam))
			continue;

		// we sometimes get false inside hits due to the movement of the shield
		// a very hacky solution is to nudge the start of the intersecting ray
		// back (proportional to how far the shield moved last frame) so as to
		// increase its length.
		// it's not 100% accurate so there's a bit of a FIXME here to do a real
		// solution (keep track in the projectile which shields it's in)
		const float3 rpvec  = ppos0 - ppos1;
		const float3 rppos0 = ppos0 + rpvec * repulser->GetDeltaDist();
		const float3 cvpos  = repulser->weaponMuzzlePos - repulser->owner->relMidPos;

		// shield volumes are always spherical, transform directly
		// (CollisionHandler will cancel out the relmidpos offset)
		if (!CCollisionHandler::DetectHit(repulser->owner, &repulser->collisionVolume, CMatrix44f{cvpos}, rppos0, ppos1, &cq))
			continue;

		if (cq.InsideHit() && repulser->IgnoreInteriorHit(wpro))
			continue;

		if (repulser->IncomingProjectile(wpro, cq.GetHitPos()))
			return;
	}
}

void CProjectileHandler::CheckUnitFeatureCollisions(bool synced)
{
	static std::vector<CUnit*> tempUnits;
	static std::vector<CFeature*> tempFeatures;
	static std::vector<CPlasmaRepulser*> tempRepulsers;

	//can't use iterators here, because instructions inside the loop modify projectiles[synced]
	for (size_t i = 0; i < projectiles[synced].size(); ++i) {
		CProjectile* p = projectiles[synced][i];

		if (!p->checkCol) continue;
		if ( p->deleteMe) continue;

		const float3 ppos0 = p->pos;
		const float3 ppos1 = p->pos + p->speed;
		// const float3 ppos1 = p->pos + p->dir * (p->speed.w + p->radius);

		quadField.GetUnitsAndFeaturesColVol(p->pos, p->speed.w + p->radius, tempUnits, tempFeatures, &tempRepulsers);

		CheckShieldCollisions (p, tempRepulsers, ppos0, ppos1); tempRepulsers.clear();
		CheckUnitCollisions   (p, tempUnits    , ppos0, ppos1); tempUnits.clear();
		CheckFeatureCollisions(p, tempFeatures , ppos0, ppos1); tempFeatures.clear();
	}
}

void CProjectileHandler::CheckGroundCollisions(bool synced)
{
	//can't use iterators here, because instructions inside the loop modify projectiles[synced]
	for (size_t i = 0; i < projectiles[synced].size(); ++i) {
		CProjectile* p = projectiles[synced][i];

		if (!p->checkCol)
			continue;

		// NOTE:
		//   if <p> is a MissileProjectile and does not have
		//   selfExplode set, tbis will cause it to never be
		//   removed (!)
		if (p->GetCollisionFlags() & Collision::NOGROUND)
			continue;

		// don't collide with ground yet if last update scheduled a bounce
		if (p->weapon && static_cast<const CWeaponProjectile*>(p)->HasScheduledBounce())
			continue;

		// NOTE:
		//   don't add p->radius to groundHeight, or most (esp. modelled)
		//   projectiles will collide with the ground one or more frames
		//   too early
		const float gy = CGround::GetHeightReal(p->pos.x, p->pos.z);
		const float py = p->pos.y;

		const bool belowGround = (py < gy);
		const bool insideWater = (py <= 0.0f);

		if (!belowGround && (!insideWater || p->ignoreWater))
			continue;

		// if position has dropped below terrain or into water
		// where we can not live, adjust it and explode us now
		// (if the projectile does not set deleteMe = true, it
		// will keep hugging the terrain)
		p->SetPosition((p->pos * XZVector) + (UpVector * mix(py, gy, belowGround)));
		p->Collision();
	}
}

void CProjectileHandler::CheckCollisions()
{
	SCOPED_TIMER("Sim::Projectiles::Collisions");

	CheckUnitFeatureCollisions(true ); // changes simulation state
	CheckUnitFeatureCollisions(false); // does not change simulation state

	CheckGroundCollisions(true ); // changes simulation state
	CheckGroundCollisions(false); // does not change simulation state
}



void CProjectileHandler::AddFlyingPiece(
	int modelType,
	const S3DModelPiece* piece,
	const CMatrix44f& m,
	const float3 pos,
	const float3 speed,
	const float2 pieceParams,
	const int2 renderParams
) {
	flyingPieces[modelType].emplace_back(piece, m, pos, speed, pieceParams, renderParams);
	resortFlyingPieces[modelType] = true;
}


void CProjectileHandler::AddNanoParticle(
	const float3 startPos,
	const float3 endPos,
	const UnitDef* unitDef,
	int teamNum,
	bool highPriority
) {
	const float priority = mix(NORMAL_NANO_PRIO, HIGH_NANO_PRIO, highPriority);
	const float emitProb = 1.0f - GetNanoParticleSaturation(priority);

	if (emitProb < guRNG.NextFloat())
		return;
	if (!unitDef->showNanoSpray)
		return;

	float3 dif = endPos - startPos;
	const float l = fastmath::apxsqrt2(dif.SqLength());

	dif /= l;
	dif += (guRNG.NextVector() * 0.15f);

	const     float3 udColor = unitDef->nanoColor;
	constexpr float  udAlpha = 20 / 256.0f; // denom=255 is not constexpr-able

	const     uint8_t* tColor = (teamHandler.Team(teamNum))->color;
	constexpr uint8_t  tAlpha = udAlpha * 256;

	const SColor colors[2] = {
		{udColor.r, udColor.g, udColor.b, udAlpha},
		{tColor[0], tColor[1], tColor[2],  tAlpha},
	};

	projMemPool.alloc<CNanoProjectile>(startPos, dif, int(l), colors[globalRendering->teamNanospray]);
}

void CProjectileHandler::AddNanoParticle(
	const float3 startPos,
	const float3 endPos,
	const UnitDef* unitDef,
	int teamNum,
	float radius,
	bool inverse,
	bool highPriority
) {
	const float priority = mix(NORMAL_NANO_PRIO, HIGH_NANO_PRIO, highPriority);
	const float emitProb = 1.0f - GetNanoParticleSaturation(priority);

	if (emitProb < guRNG.NextFloat())
		return;
	if (!unitDef->showNanoSpray)
		return;

	float3 dif = endPos - startPos;
	const float len = fastmath::apxsqrt2(dif.SqLength());

	dif /= len;
	dif += (guRNG.NextVector() * (radius / len));

	const     float3 udColor = unitDef->nanoColor;
	constexpr float  udAlpha = 20 / 256.0f;

	const     uint8_t* tColor = (teamHandler.Team(teamNum))->color;
	constexpr uint8_t  tAlpha = udAlpha * 256;

	const SColor colors[2] = {
		{udColor.r, udColor.g, udColor.b, udAlpha},
		{tColor[0], tColor[1], tColor[2],  tAlpha},
	};

	if (!inverse) {
		projMemPool.alloc<CNanoProjectile>(startPos, dif * 3.0f, int(len / 3.0f), colors[globalRendering->teamNanospray]);
	} else {
		projMemPool.alloc<CNanoProjectile>(startPos + dif * len, -dif * 3.0f, int(len / 3.0f), colors[globalRendering->teamNanospray]);
	}
}

float CProjectileHandler::GetParticleSaturation(bool randomized) const
{
	const int curParticles = GetCurrentParticles();

	// use the random mult to weaken the max limit a little
	// so the chance is better spread when being close to the limit
	// i.e. when there are rockets that spam CEGs this gives smaller CEGs still a chance
	const float total = std::max(1.0f, maxParticles * 1.0f);
	const float fract = curParticles / total;
	const float rmult = 1.0f + (int(randomized) * 0.3f * guRNG.NextFloat());

	return (fract * rmult);
}

int CProjectileHandler::GetCurrentParticles() const
{
	// use precached part of particles count calculation that else becomes very heavy
	// example where it matters: (in ZK) /cheat /give 20 armraven -> shoot ground
	for (size_t i = frameProjectileCounts[true], e = projectiles[true].size(); i < e; ++i) {
		frameCurrentParticles += projectiles[true][i]->GetProjectilesCount();
	}
	frameProjectileCounts[true ] = projectiles[true ].size();

	for (size_t i = frameProjectileCounts[false], e = projectiles[false].size(); i < e; ++i) {
		frameCurrentParticles += projectiles[false][i]->GetProjectilesCount();
	}
	frameProjectileCounts[false] = projectiles[false].size();

	int partCount = frameCurrentParticles;
	for (const auto& c: flyingPieces) {
		for (const auto& fp: c) {
			partCount += fp.GetDrawCallCount();
		}
	}
	partCount += groundFlashes.size();
	partCount += projectileRegistry.size();
	partCount += simpleParticleSystem.NumParticles();
	return partCount;
}

