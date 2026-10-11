// Only the empty, black background is replaced. Native reverse-Z depth
// protects foreground geometry; menus are gated by the companion plugin.
uniform bool SkyEnabled < hidden = true; > = false;
uniform float3 CameraRight < hidden = true; > = float3(1,0,0);
uniform float3 CameraUp < hidden = true; > = float3(0,1,0);
uniform float3 CameraBack < hidden = true; > = float3(0,0,1);
uniform float4 CameraProjection < hidden = true; > = float4(1,1,0,0);
uniform float SkyBrightness < hidden = true; > = 0.7;
uniform float SkyAct < hidden = true; > = 1;
uniform float SkyNightMix < hidden = true; > = 0;
uniform float3 CameraEye < hidden = true; > = float3(0,0,0);
uniform float3 CameraAnchor < hidden = true; > = float3(0,0,0);
uniform float InteriorProfile < hidden = true; > = 0;
uniform float InteriorHeight < hidden = true; > = 32;
uniform float InteriorBrightness < hidden = true; > = 1.3;

texture2D SceneColor : COLOR;
texture2D SceneDepth : DEPTH;
sampler2D ColorSampler { Texture=SceneColor; };
sampler2D DepthSampler { Texture=SceneDepth; MinFilter=POINT; MagFilter=POINT; };
texture2D Act1Sky < source="act1-overcast.jpg"; > { Width=4096; Height=2048; Format=RGBA8; };
sampler2D SkySampler { Texture=Act1Sky; AddressU=WRAP; AddressV=CLAMP; };
texture2D Act2Sky < source="act2-sunny.jpg"; > { Width=4096; Height=2048; Format=RGBA8; };
sampler2D DesertSampler { Texture=Act2Sky; AddressU=WRAP; AddressV=CLAMP; };
texture2D Act3Sky < source="act3-hazy.jpg"; > { Width=4096; Height=2048; Format=RGBA8; };
sampler2D JungleSampler { Texture=Act3Sky; AddressU=WRAP; AddressV=CLAMP; };
texture2D Act5Sky < source="act5-cold-clouds.jpg"; > { Width=4096; Height=2048; Format=RGBA8; };
sampler2D SnowSampler { Texture=Act5Sky; AddressU=WRAP; AddressV=CLAMP; };
// Retain 8K for small star points on wide/high-resolution displays.
texture2D NightSky < source="starry-night.jpg"; > { Width=8192; Height=4096; Format=RGBA8; };
sampler2D NightSampler { Texture=NightSky; AddressU=WRAP; AddressV=CLAMP; };

float3 StarryNight(float2 pano,float elevation) {
    // Keep the stars and Milky Way, suppress the photograph's distant city
    // lights at the horizon, and give the quest darkness a deep blue night.
    const float3 photo=tex2D(NightSampler,pano).rgb;
    const float3 stars=pow(max(photo-0.24,0),1.35)*float3(0.68,0.75,0.95);
    return lerp(float3(0.006,0.009,0.018),stars,smoothstep(0.10,0.32,elevation));
}
float3 ActSky(float2 pano,float3 ray) {
    float3 sky;
    if (SkyAct==2) {
        sky=lerp(tex2D(DesertSampler,pano).rgb,StarryNight(pano,ray.y),SkyNightMix);
    } else if (SkyAct==3) {
        sky=tex2D(JungleSampler,pano).rgb*float3(0.84,0.91,0.87);
        sky*=lerp(1.0,0.25,SkyNightMix);
    } else if (SkyAct==4) {
        const float smoke=tex2D(SkySampler,pano).r;
        const float glow=exp(-max(ray.y,0)*7)*(0.35+0.65*smoke*smoke);
        sky=float3(0.002,0.001,0.003)+float3(0.12,0.017,0.003)*glow;
        sky+=StarryNight(pano,ray.y)*0.10;
    } else if (SkyAct==5) {
        sky=tex2D(SnowSampler,pano).rgb*float3(0.78,0.88,1.0);
        sky*=lerp(1.0,0.30,SkyNightMix);
    } else {
        sky=tex2D(SkySampler,pano).rgb;
        sky*=lerp(1.0,0.25,SkyNightMix);
    }
    return sky*SkyBrightness;
}

// Original procedural materials: no repeated photograph of a room, and no
// new texture allocation. World-space sampling makes the surface move past
// the camera naturally as the hero walks instead of sticking to the screen.
float StoneHash(float2 p) {
    float3 q=frac(float3(p.x,p.y,p.x)*0.1031);
    q+=dot(q,q.yzx+33.33);
    return frac((q.x+q.y)*q.z);
}
float StoneNoise(float2 p) {
    const float2 cell=floor(p),f=frac(p);
    const float2 u=f*f*(3-2*f);
    return lerp(lerp(StoneHash(cell),StoneHash(cell+float2(1,0)),u.x),
                lerp(StoneHash(cell+float2(0,1)),StoneHash(cell+1),u.x),u.y);
}
float RoofRelief(float2 p) {
    if (InteriorProfile==1 || InteriorProfile==6 || InteriorProfile==7)
        return 3.5*StoneNoise(p*0.09)+1.5*StoneNoise(p*0.24);
    // Broad barrel-vault bays for built interiors, rather than a flat lid.
    return 6*sin(frac(p.x/36)*3.14159265);
}
float3 InteriorTint() {
    if (InteriorProfile==3) return float3(0.34,0.27,0.18);
    if (InteriorProfile==4) return float3(0.17,0.22,0.19);
    if (InteriorProfile==5) return float3(0.19,0.24,0.19);
    if (InteriorProfile==6) return float3(0.25,0.38,0.48);
    if (InteriorProfile==7) return float3(0.19,0.10,0.08);
    return InteriorProfile==1?float3(0.29,0.25,0.21):float3(0.26,0.25,0.24);
}
float StoneMaterial(float2 p) {
    const float grain=0.55+0.25*StoneNoise(p*0.4)+0.15*StoneNoise(p*1.7);
    if (InteriorProfile==1 || InteriorProfile==6 || InteriorProfile==7) {
        const float ridge=abs(StoneNoise(p*0.19)-0.5);
        return grain*(0.45+0.55*smoothstep(0.018,0.10,ridge));
    }
    float2 bricks=p/float2(8,3.5);
    bricks.x+=frac(floor(bricks.y)*0.5);
    const float2 edge=min(frac(bricks),1-frac(bricks));
    const float mortar=smoothstep(0.015,0.05,min(edge.x,edge.y));
    return grain*lerp(0.2,0.75+0.25*StoneHash(floor(bricks)),mortar);
}
float3 InteriorBackground(float3 ray) {
    if (InteriorProfile==8) {
        // Arcane Sanctuary is suspended in space and keeps an open void.
        const float2 pano=float2(atan2(ray.z,ray.x)/6.28318530718+0.5,acos(clamp(ray.y,-1,1))/3.14159265);
        return StarryNight(pano,abs(ray.y))*0.35+float3(0.003,0.002,0.008);
    }
    const float3 eye=CameraEye,anchor=CameraAnchor;
    const float3 local=eye-anchor;
    const float ceiling=anchor.y+max(InteriorHeight,local.y+12);
    // An enclosure beyond the game's drawn walls closes distant/background
    // gaps. It has no collision and never replaces a native depth surface.
    const float a=dot(ray.xz,ray.xz),b=dot(local.xz,ray.xz);
    const float c=dot(local.xz,local.xz)-160*160;
    const float wall=a>0.00001?max((-b+sqrt(max(b*b-a*c,0)))/a,0.1):100000;
    const float floorHit=ray.y<-0.001?(anchor.y-6-eye.y)/ray.y:100000;
    float roof=ray.y>0.001?(ceiling-eye.y)/ray.y:100000;
    if (roof<wall) {
        [unroll] for (int i=0;i<4;++i)
            roof=(ceiling+RoofRelief((eye+ray*roof).xz)-eye.y)/max(ray.y,0.001);
    }
    const float distance=min(wall,min(roof,floorHit));
    const float3 p=eye+ray*distance;
    float2 surface;
    float3 normal;
    if (roof<=wall && roof<=floorHit) {
        surface=p.xz;
        const float h=RoofRelief(p.xz);
        normal=normalize(float3(RoofRelief(p.xz+float2(0.25,0))-h,-0.25,RoofRelief(p.xz+float2(0,0.25))-h));
    } else if (floorHit<wall) {
        surface=p.xz; normal=float3(0,1,0);
    } else {
        surface=float2(atan2(p.z-anchor.z,p.x-anchor.x)*160,p.y);
        normal=normalize(float3(anchor.x-p.x,0,anchor.z-p.z));
    }
    const float3 light=anchor+float3(0,8,0)-p;
    const float diffuse=0.25+0.75*saturate(dot(normal,normalize(light)));
    const float attenuation=1/(1+dot(light,light)/(45*45));
    const float haze=smoothstep(65,180,distance);
    const float3 tint=InteriorTint();
    const float3 lit=tint*StoneMaterial(surface)*(0.06+diffuse*attenuation*0.9)*float3(1.08,0.98,0.85);
    return lerp(lit,tint*0.018,haze)*InteriorBrightness;
}

void SkyVS(uint id:SV_VertexID,out float4 position:SV_Position,out float2 uv:TEXCOORD) {
    uv=float2((id==1)?2:0,(id==2)?2:0);
    position=float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
float4 SkyPS(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target {
    const float4 color=tex2D(ColorSampler,uv);
    if (!SkyEnabled || tex2D(DepthSampler,uv).r>0.0000001) return color;
    const float3 viewRay=float3((uv.x*2-1+CameraProjection.z)/CameraProjection.x,(1-uv.y*2+CameraProjection.w)/CameraProjection.y,-1);
    const float3 ray=normalize(viewRay.x*CameraRight+viewRay.y*CameraUp+viewRay.z*CameraBack);
    const float black=max(color.r,max(color.g,color.b));
    if (InteriorProfile>0) {
        const float mask=1-smoothstep(0.03,0.10,black);
        if (mask<=0) return color;
        return float4(lerp(color.rgb,InteriorBackground(ray),mask),color.a);
    }
    // The lower hemisphere stays dark; the reflected ground of a pure-sky
    // panorama is not used as terrain. Fade gently through the horizon.
    const float above=smoothstep(-0.035,0.015,ray.y);
    const float mask=above*(1-smoothstep(0.03,0.10,black));
    if (mask<=0) return color;
    const float2 pano=float2(atan2(ray.z,ray.x)/6.28318530718+0.5,acos(clamp(ray.y,-1,1))/3.14159265359);
    const float3 sky=ActSky(pano,ray);
    return float4(lerp(color.rgb,sky,mask),color.a);
}
technique D2R3DSky < enabled=true; ui_label="D2R-3D skies and interiors"; > {
    pass { VertexShader=SkyVS; PixelShader=SkyPS; }
}
