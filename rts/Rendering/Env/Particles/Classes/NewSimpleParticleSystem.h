/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef NEW_SIMPLE_PARTICLE_SYSTEM_H
#define NEW_SIMPLE_PARTICLE_SYSTEM_H

#include "Sim/Projectiles/Projectile.h"
#include "Rendering/Textures/TextureAtlas.h"
#include "System/float3.h"
#include "SimpleParticleSystem.h"
#include "Rendering/GL/RenderBuffers.h"

class CUnit;
class CColorMap;

struct DeletedEntity
{};

struct NewSimpleParticle
{
	float3 pos;
	float3 speed;

	float rotVal;
	float rotVel;

	float life;
	float decayrate;
	float size;
	//float sizeGrowth;
	//float sizeMod;
	
	bool deleteMe=false;
};

class NewSimpleParticleSystem
{
public:
	NewSimpleParticleSystem();

	void from(const CSimpleParticleSystem& other) {
		emitVector = other.emitVector;
		emitMul = other.emitMul;
		gravity = other.gravity;
		particleSpeed = other.particleSpeed;
		particleSpeedSpread = other.particleSpeedSpread;
		emitRot = other.emitRot;
		emitRotSpread = other.emitRotSpread;
		texture = other.texture;
		colorMap = other.colorMap;
		directional = other.directional;
		particleLife = other.particleLife;
		particleLifeSpread = other.particleLifeSpread;
		particleSize = other.particleSize;
		particleSizeSpread = other.particleSizeSpread;
		airdrag = other.airdrag;
		sizeGrowth = other.sizeGrowth;
		sizeMod = other.sizeMod;
		numParticles = other.numParticles;		
		
		pos = other.pos;
		speed = other.speed;
		
		createFrame = other.createFrame;
			
		allyteamID = other.allyteamID;
		useAirLos = other.useAirLos;
		
		rotParams = other.rotParams;
		animParams = other.animParams;
	}

	void Draw() ;
	float3 GetDrawPos(float t) const { return (speed.w != 0.0f) ? (pos + speed * t) : pos; }
    float GetDrawRadius() const { return drawRadius; }
    int GetAllyteamID() const { return allyteamID; }    
	bool Update(NewSimpleParticle& p) ;
	void Init(const CUnit* owner, const float3& offset) ;
	void InitParticle(NewSimpleParticle& p, const float3& offset);
	void DrawParticle(const NewSimpleParticle* p);
	
	void DrawOnMinimap() const;
    void AddMiniMapVertices(VA_TYPE_C&& v1, VA_TYPE_C&& v2) const;
public:
	void AddEffectsQuad(const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl) const;
	void UpdateAnimParams();
	
	void SetPosition(const float3& p) {   pos = p; }
	void SetVelocity(const float3& v) { speed = v; }
	float SetSpeed(const float3& v) { return (speed.w = v.Length()); }	
	void SetVelocityAndSpeed(const float3& v) {
		SetVelocity(v);
		SetSpeed(v);
	}
	void UpdateRotation();
		
	float3 emitVector;
	float3 emitMul;
	float3 gravity;
	float drawRadius;
	
	float particleSpeed;
	float particleSpeedSpread;

	float emitRot;
	float emitRotSpread;

	AtlasedTexture* texture;
	CColorMap* colorMap;
	bool directional;

	float particleLife;
	float particleLifeSpread;
	float particleSize;
	float particleSizeSpread;
	float airdrag;
	float sizeGrowth;
	float sizeMod;

	int numParticles;
	
	// from base
	float3 pos;
	float4 speed;
	float3 drawPos;
	
	// TODO create new component Animation?
	float3 animParams = { 1.0f, 1.0f, 30.0f }; // numX, numY, animLength, 
	float animProgress = 0.0f; // animProgress = (gf_dt % animLength) / animLength
	float3 rotParams = { 0.0f, 0.0f, 0.0f }; // speed, accel, startRot |deg/s, deg/s2, deg|

	float rotVal = 0.0f;
	float rotVel = 0.0f;

	int createFrame;
	
	// todo only needed during init
	float3 up;
	float3 right;
	float3 forward;

	int allyteamID = -1;
	bool useAirLos = false;
};

#endif // NEW_SIMPLE_PARTICLE_SYSTEM_H
