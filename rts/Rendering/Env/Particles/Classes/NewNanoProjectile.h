/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef NEW_NANO_PROJECTILE_H
#define NEW_NANO_PROJECTILE_H

// #include "Sim/Projectiles/Projectile.h"
#include "System/Color.h"
#include "Rendering/GL/RenderBuffers.h"

class NewNanoProjectile // : public CProjectile
{
public:
	NewNanoProjectile(float3 pos, float3 speed, int lifeTime, SColor color);

	void Update();
	void Draw();
	void DrawOnMinimap() const;
    float3 GetDrawPos(float t) const { return (speed.w != 0.0f) ? (pos + speed * t) : pos; }
    float GetDrawRadius() const { return drawRadius; }
    int GetAllyteamID() const { return allyteamID; }

private:
    void AddMiniMapVertices(VA_TYPE_C&& v1, VA_TYPE_C&& v2) const;
	float rotAcc = 0.0f;

	float rotVal0x = 0.0f;
	float rotVel0x = 0.0f;
	float rotAcc0x = 0.0f;
public:
  
	const static inline float rotVal0 = 0.0f;
	const static inline float rotVel0 = 0.0f;
	const static inline float rotAcc0 = 0.0f;
	const static inline float rotValRng0 = 0.0f;
	const static inline float rotVelRng0 = 0.0f;
	const static inline float rotAccRng0 = 0.0f;
   
	
    
	static const int drawRadius = 3;
    
    float rotVal = 0.0f;
    float rotVel = 0.0f;
    float3 drawPos;
    const SColor color;   
    
    float3 pos;
    float4 speed;
    bool deleteMe=false;
    
    const int createFrame;   
    const int deathFrame;        
    
    float animProgress = 0.0f;
    float3 animParams = { 1.0f, 1.0f, 30.0f }; // numX, numY, animLength, 
    const int allyteamID = -1;
    
    
// funcs from base
    void AddEffectsQuad(const VA_TYPE_TC& tl, const VA_TYPE_TC& tr, const VA_TYPE_TC& br, const VA_TYPE_TC& bl) const;
    TypedRenderBuffer<VA_TYPE_PROJ>& GetPrimaryRenderBuffer() const;
};

#endif /* NEW_NANO_PROJECTILE_H */

