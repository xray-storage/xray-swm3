#ifndef RenderVisual_included
#define RenderVisual_included
#pragma once

class IKinematics;
class IKinematicsAnimated;
class IParticleCustom;
struct vis_data;

class IRenderVisual
{
public:
    virtual ~IRenderVisual() { ; }
    virtual vis_data& getVisData() = 0;
    virtual u32 getType() = 0;

#ifdef DEBUG
    virtual shared_str getDebugName() = 0;
#endif

    virtual IRenderVisual* getSubModel(u8 idx) { return 0; } //--#SM+#--
    virtual IKinematics* dcast_PKinematics() { return 0; }
    virtual IKinematicsAnimated* dcast_PKinematicsAnimated() { return 0; }
    virtual IParticleCustom* dcast_ParticleCustom() { return 0; }
};

#endif //	RenderVisual_included
