# Graphics papers paraphrase learning
!!!! this is not a truth i whould follow. this is just me writhing out what i have learned and as this is a school task and my teachers love any and all .md file i have decided to leave it in the repo, i have not checked gramar in any way i have not had the document reviewed by anyone so anything can be wrong. ;)

# forward+ and defered lighting tech

ground work light rendering is split mostly in to 2 difrant types first being forward+ and other being defered, lets start with forward+. forward+ is a light culling and rendering technic that works in scene space, say we have a scene with a [point light](#point-light) and 5 block meshes, the lighting is applyed per light in scene X all the objects in the scene, regardless of how the camera is placed or angled. while forward+ works in scene defered meens applying light per pixel on the scene and operates on the camera, take the same example light is applyed per light visble and per pixel in the [G buffer](#g-buffer). mening that larger scenes where most lights are hiden / have no effect on the lighting visble in the scene we wil gain substansial preformace, this is revulutunary allowing masive scenes with any amount of lights. with that why do we stil use forward+ if defered is just better? becus of the limited infrmation inside the g buffer, defered stops working when we look at transparancy, should we apply on the object? the object behind? create a blend space? we chould ofc try to solv this but her its just better so exclud it from the defered lighting and use forward+ insted since its created to be applyed per object so in senarios where we nead object interactions forward is the way to go.

with that baseline of how lighting is applyed we can start looking at difrant technices used on top of this. im talking about AA, AO, SSS, SSR, and ultimatly GI all build mostly on the G buffer.

# PBR and BRDF
PBR is a form of rendeirng designed to prodcue renders matching real world views, Physics based rendering, a realy big part of this comes from light and how it interacts with materials. for this we have a "rulebook" we call BRDF. BRDF defines how light interacts with materials. it takes parms like light direction and intensity and then applyes that to materails using there respective mat params like rughfness to controll how the energy is redetrebuted across that materail, creating [specular highlights](#specular-highlights) and similar, in consept it might seem simile, like oh yea so just how light makes objects shine, but in while the constep is simiple the execution of that is not. to demonstrate this is the formula for specular highlights: 

f(l, v) = diffuse + D(θh)F(θd)G(θl, θv) / 4 cos θl cos θv

not so simple now huh. you do not nead to understand this formula but, it uses a prinsiple of [half vectors](#half-vector), where if the specular angle is matching the "half vector" of your frame it knows where to apply any specular hints. 
![Screenshot](schematics/BRDF.png)
this is a BRDF slice of red plastic and specular red plastic side by side along with a schematic view. (schematic by Walt Disney Animation Studios)
### Mcrofacet
microfacet the observation that all surfaces that look smooth to the naked eye if zoomed up on wil have bumps / facets, a good way i read that helped me visulize it was imagine your in a helicopter looking down at a lake, from your view it looks flat / smooth, but once you get closer you find the waves or facets. these groves in the materal is whats laragaly responsible for specular highlights and is what the above schematic and formula is trying to replecate in engine.

### MERL 100
this is an image slice of the merl 100 database a set of BRDF's that we use to as a baseline for almost all specularity.
![Screenshot](schematics/MERL100.png)


# IBL
IBL is an shading and image from it stands for Image based lighting, its a technec used by amost all major engines such as Unreal and unity, along with a lot of inhouse engines. its in a way the glue that ties and streamlines PBR and controlls specularty in a way thats super close to realisam. ok so for the techincal part this is what i learned from reading UE4's IBL aproch and i recomend reading that for anyone wanting to  learn this, so in short UE4 splits the specular IBL interals in to 2 difrant parts, part 1 being a pre filtered enviorment map thats jsut cubemap mips by roughness then second part in the real stuff, environment BRDF, a 2D LUT with Rughness, NoV / scale, bias to F0. then we spred out the GGX distribution, store rughness to mip, and her we just assume that N=V=R. ok now for the BRDF LUT; we use R16G16 format storing scale and bias values for the split sum, then we diffuse lambertian via irradiace cubemap pre convolved with a cosine kernel. emm just read what i wrote and this might be a slight step up in complexity if you understand that you can basicly just create your own IBL lol. but the core of its this the cool guys at UE have spent a lot of time going over all of the difrant algorytems for everything. so yea just to finish this mess, next step is key approximation thats just preventing long grazing angles in reflections and her we use an isotropic method"N=V"  then just for some resion we just weight it by NoL inside the pre filter pass, cus acording to UE that gives better visual results.

# Anti Aliasing

### FXAA
so we have a few types of AA one of them i just descovered and is more experamental but fun to look at, so lets start at the bottom with FXAA, fast aproxemet AA, this is the beerbones simple for of AA it works by finding jaged edges and applying blur on top of them, so not fixing them in any way but hiding and turing focus away from them, why use this form of AA? while it is a week AA its super cheep with only 1 pass, so on older hardwhere its the perfect choice when other technics are wayy to expensive.

### MSAA
so next one is MSAA, multi sampling AA is a tech where you locate edges and sample them incresing there res this is in all ways “fixing” the jaged edges, with a higher rendering res only on edges we can hide a lot of the cluter with 0 sacrefice in other areas of the scene, this is not hiding like FX is but smoothing out at the cost of a higher rendering count, MSAA comes then in multiple scales where you can apply a 2X sampling on edges or 32X for that case allowing you to easly choise what level you want and easy control of preformace. 

### TAA
so now for the modern way of solving this tho more complecated compared to FX and MS, we have TAA or Temporal AA, Temporal meens real time this is not an effect applyed after the image is created or a blur stacked on top, TAA works by reusing data from previuse frames and using that data to stablize the image while its created, this meens 0 passes, 0 effects, but at the cost of memory overhead from storing that frame data, it also uses very little read of the G buffer allowing saving a lot. TAA is widly regared as the go to AA for modern engines. any flaws? yes but only minor, we have a fenomenon we call ghosting wich is somthing that happens when objects move fast since data from preciuse frames are reused to stablize image when an object moves to fast it can leave a ghost trail behind it where data from its previuse posisions blead thure to the finial frame, this has goten better with the years but  is stil there, tho many argues that the ghost trail is not a problem its hard to see, and using entire post pass to cleen it up just is not worth it most of the time. 

### how does TAA function? 
the main use is to reuse subpixel samples from past frames, in a way achiving [supersampling](#supersampling). in frame N-1 we have taken a few samples from each pixel, avaraging them to 1 color value per pixel and storing that. this is the resample step, we then take [motion vectors](#motion-vectors) from the renderer to acumalate for any movment, using the motion vectors from N and the data from N-1 we can validate that data(rectify) to find if that data is usable, fixable, or neads to be dumped. in the rectification we take color samples from N to mach them upagains the singel color per pixel we stored from N-1. why? well in some cases scenes can take drastical changes in occlusion, lighting or similar effectvly rendering some of our color data useless or unaccorate, without this step fenomenons like ghosting will be way to large.
![Screenshot](schematics/TAA.png)
schematic by Lei Yang, Shiqiu Liu, Marco Salvi. NVIDIA Corporation(2020) 


### Jitter sampling
so in TAA a common aproch is to sample in a set sequence, and to help get more screen covarage we do somithng called jitter sampling, where we ofset the [projection matrix](#projection-matrix) each frame wich then ofsets the sampling, so for every pixel we add a viewport sub pixel jitter for each pixel, every frame.

### AGAA
now for the cool one i mensioned its a tech released by nivida the paper i read was realeced in 2015, and its called AGAA, Aggregated G buffer AA, i will not try to explan it cus it makes 0 sens but, its an alternative to MSAA giving similar resaults at way les cost, AGAA with 2 aggregates per pixel gives simiar results to 32XMSAA while using 54% less memory and is up to 2.6X faster! if you find this cool you can read about it her: [AGAA Paper nvidia](https://research.nvidia.com/publication/2015-02_aggregate-g-buffer-anti-aliasing)


# shadows
we have a few difrant ways of doing shadows and what we use mostly boils down to what senario / scene we are in, take a dir light(mostly used for sun or larger areas) this will be used to light up a large ofthen outdoors scene, thus we nead some optimisation that is where Cascades come in.

### CSM cascade shadow map
we separeate in to distance fealds/ cascades and and render shadows at difrant res for each cascade region, a good way of looking at it is CSM is mesh LOD for shadows. so the terum of this is realy simple. now unlike mesh LODs we do not have a cascde 1 dist, 2 dist... that is not how its done, if you look in the cascade debug view youl see that there is no straight line, the cascades are like waves. this is becuse cascades are in view space, thus projected on to stuff, that projection causes warping or in other words waves.

### PCF
PCF is a way of filtering shadows in a way to reduse visibity aliasing, where each shadow sample is just eather 0 no shadow or 1 shadow, we can smoth that out, take a line of shadows like: (0 0 0 1 1 1), PCF takes the starting point and end point and avg them out creating a smooth presentage ramp, so it turns to (0 0.2 0.4 0.6 0.8 1) and boom now shadow no longer alasing or at lest its smoothed a bit. 

### PCSS
now PCSS is just a better version of PCF tho its more expensive so you might not want to always use it. imagine a ball on the ground as the ball is laing on the ground the shadow should be realy dark, but if you were to move that ball up 3M in the air the shadow gets dimed down cus of how light transportation bleeds and in real life is not a constant RAY boom shadow there. so PCSS takes a shadow, finds the object depth of the shadows owner and makes the shadow more or less transparant based on that depth, its is a 
way a smart object awere version of PCF but at the cost of checking the depth for each shadow object.

### VSM
VSM is a more modern way of filtering shadows, its designed to give smoother shadows like PCF but be fast and simple, insted of storing just depth we store depth and depth squred "momment representaion of depth". storing 2 values allows use find find variace"how far spred out the difrant shadow values are"  we can then using nothing but math find out if a region is mostly flat or mixed. then just compute the Probablity upper bounds with Chebyshev’s inequality"this might be kinda advanced but you dont nead to fully understand it". ok so any problems with VSM? yes so when variace is to high the math can be unsertan, and the shadows can be to soft. so in human terms that meens light can leek and show thure objects cus VSM dont know that oh this her is 2 difrant surfaces. a few ways to fix VSM is, clamping variance to limit so it never explodes, we can store depth exponentaly EVSM thus reduce blending, and we can also store more moments for a more accurate resault MVSM.

### Shadow Bias
shadow maps sufer from some major problems like self shadowing artifacts"shadow acne" and floating shadows"peter panning". so insted of rendeing shadow right up to the source we add a small bias so that it does not touch, this fixes both these problems, its a simple thing but super important.

### contact shadows
so shadow maps are good ad larger shadows but closer up contact shadows not so much, think like a chair on the ground the legs that whould be a contact shadow, anywhere with close contact points, why are shadow maps like this? even if you have good csm that dont mater cus shadow maps are low res and they have to be, so thats why we nead this.


# SS, Screen space Effects
now i will cover my view on most of the major SS effects, like SSR, SSAO, SSGI, and SVGF. this is a lot of info and long reaseached technics baked into it.

### SSAO
Ambient occlusion is a lighting technic designed to calculate the amount of light that reaches a point on a [defused surface](#diffuse-surface) in acordance with its [occluders](#occluders). it operates on the [depth buffer](#depth-buffer) and acosiates with [per pixel normals](#per-pixel-normals).
in most cases AO is given a point P, and a hemosphere Ω with a radus for the oclusion area, the Ω is then oriented to the normal of N -> P so that a light cast can reflect and create our occlusion area.
![Screenshot](schematics/AO.png)
her is a an example where the objects are cut at 90 degres where angel bias is an applyed bias to limit occluison bouce angel thus can be used as a paramater for AO.
![Screenshot](schematics/AO2.png)
all this is good and all but how is this SS? its not yet, this is how we calculate the occlusion area the SS comes from where we obtain data from, insted of doing a 3d lookup in 3d space we create a 2d hightmap as a depth map that we use to find angles around our scene, thus unlike most schematics this one is almost 1 to 1 with the effect we actauly apply in engine. 

## SSR
SSR or scren space reflection is a way of creating reflections often done by casting rays, there are many difrant ways of doing this first one is just direct ray to object image reflections, here we cast a ray from the object to specular sufrace then bouce it to the cam, drawing an image of the orgin object relative to both the cam and the bounce surface. this is the simplest form of SSR and is kinda outdated


# VOLUMETRICS
volumetrics the act of rendering effects in 3d space, as a volume instead of a surface, where all the other effects I have talked don't exist in 3d space at the same level of volumetrics, volumetrics are effects that live in a a volume. think fog, or some types of realistic clouds or stuff like that. 
## Transmittance
transmittance is how light passes through a volume in a way the opacity of that volume how much light is absorbed "loss of energy", and out scattering, out scattering is when photons are redirected away from the viewers eyes / cam due to particle interference inside the passing volume, this results in a loss of radiance / intensity. 
## Scattering
scattering is the physical act where photons move trough a volume and is then redirected by microscopic particles inside the volume, its a core effect of volumetrics and bake a lot of the realism. we have different types of scattering, one I mentioned before that is out scattering when photons are moved away from there original path, or in scattering where particles that was not going to hit the camera are redirected in to the line of sight of the viewer, this is what created effects like god rays. 
## Scatter Coefficient
when we talk about scattering a element is how likely photons are to scatter, this is called scatter coefficient (σₛ), a higher coefficient means more likely to scatter, and this controls both in and out scatter.
##  Extinction Coefficient
extinction coefficient is how energy is removed from a beam as it travels, this happens via 2 different things, absorption and scattering. both absorption and scatter both contribute to the decay of the beam and there for total extinction coefficient is just the sum of the to: 
### σₜ = σₐ + σₛ 

the total coefficient is used inside beer's lambert law to calculate the end Transmittance and again transmittance is just the fraction of light that survives traveling through a volume. and just as like a finishing touch just in case I wasn't clear on this, scatter is energy redistributed, and absorption is energy destroyed. 
Basic transmittance formula:
### T = e^(-σₜ * s) 

#### σ = Coefficient
#### σₛ = Scatter Coefficient
#### σₐ = absorption Coefficient

# Real-Time Volumetrics
this part will be super math heavy. we will now look a way of doing light I read about in a frostbite paper. single scatter rendering.
Schematic by EA: Frostbite Team
![[Volumetrics1.png]]
now what the actual F am I looking at???? so we got the cam on left(X) and we have the hitting surface on the right(XS) then we have the sun above with 3 blue rays, the green dots are scatter points, the red hour glass looking things are the dual sided scatter directions, and last the big orange line is the view ray from cam.
now what do we do with this info, so were trying to figure out the light intensity okey or Li​. that's split in to 2 major parts the fog contribution and surface contribution.
$$
T_r(x, x_s)\,L_s(x_s, \omega_o)
$$
that's for surface lighting, break down: so Tr stands for Transmittance and as you can see we input X and XS in there so that easy just Transmittance over the distance from cam to surface. then we have Ls, Light scatter, in this schematic we have 3 scatter points. the sun is shooting out 4 rays, one of then is hitting the wall (XS) that then happened to bounce back to the back along the orange line, now as that beam travels energy from it is being scattered by the 3 other rays "blue ray", that's the Ls, so 3 times we loss energy from crashing particles, and XS there is just  the starting point and ωo the scatter points. so what we get is the amount of light that reach our cam from the the surface. 
$$
\int_{0}^{S} T_r(x, x_t)\,\sigma_t(x)\,L_{scat}(x_t, \omega_i)\,dt
$$
and now my brain hurts again, so what's this? we se a lot of familiar stuff, presuming you actually read the entire ting, we see: Tr, σt, Lscat. This part is the real volumetric part, Fog Glow accumulation. this formula is walking through the camera ray and gathering scattered light. 

this looks worse then it is, I know the integral is scary but all that means is that this is done over time. so accumulate over distance. okey breakdown: Tr again this is just Transmittance or the light that survived / reached the viewer, or more specifically how much of the already scattered light is going to survive to reach the viewer. then we have σt this is the extinction Coefficient or how dense the volume is. and Lscat is the scattered light at the current sample. here is the lower Lscat equation:
$$
L_{scat}(x_t, \omega_i)
= \rho \sum_{l} f(v, l)\, V_{is}(x, l)\, L_i(x, l)
$$
Okey so this broken down Li is the light intensity we get from the light source. Vis is shadow visibility so it checks if is the sample shadowed? then the important part of this equation, f(v,l), this controls the scatter direction, in our example the scatter direction is a bi-directional forward backwards scatter, as marked by the red hourglass shaped drops on each scatter point. we only have 3 types of scatter, that's forward, backwards, and isotropic scatter.

## beer-lambert law
Schematic by Ubisoft
![[beer-lambert.png]]
$$
T_{A \to B} = e^{-\int_{A}^{B} \beta_e(x)\,dx}
$$
ok so iv already explained this but here is the science accurate way, this is beer-Lambert law, a physics law that defines Transmittance, and describes extinction of incoming light, out scattering.

## different scattering types
schematic by Ubisoft
![[scattertypes.png]]
so here we got 2 types of scatter, one being Rayleigh scattering which is like smaller particles, as the schem shows you see all the small black particles, these are stuff like air particles, and the scattering from this is what makes sky blue! and then there is Mie scattering which is bigger particles like dust or aerosol, this one is way denser has a lot of absorption, though less scattering compared to Rayleigh. 
## phase functions
Phase function is a function that defines what scatter direction is preferred for that given volume. one good way of looking at it is, Phase functions are similar to BDRFs given the medium retunes directional probability. the simplest of phase functions, isotropic scattering, and equal chances for all scatter directions.

one phase function that's well known is Henyey Greenstein HG phase function. it has 1 parameter, G, that controls the scattering so 0 is isotropic, > 0 is forward, and < 0 is backwards. 
$$
p(\cos\theta) = \frac{1 - g^2}{4\pi \left(1 + g^2 - 2g\cos\theta \right)^{3/2}}
$$
in reality there is a lot that happens and to simulate actual real scattering is insanely expensive, so in real time engine design we usually go for phase functions that are cheep, easy to use / few parameters, gives good control, HG gives does all this and is so simple artists can use it as a tool without understanding it, because to an artists eyes, oh this 1 setting controls if light goes forward backwards or stand still "not factually correct but that's what you tell your artist", and that's good enough for most engines.

---

# Glossary

A lookup table for terms used throughout this document. Click any linked term in the text to jump here.

| Term | Definition |
|------|------------|
| <a id="depth-buffer"></a>**Depth Buffer** | A per-pixel buffer that stores the distance from the camera to the nearest surface at each pixel. Screen-space effects like SSAO read from it to know where surfaces sit in the scene without re-processing the 3D geometry. |
| <a id="diffuse-surface"></a>**Diffuse Surface** | A surface that scatters incoming light equally in all directions regardless of viewing angle, giving a matte, non-shiny appearance. The opposite of a specular/glossy surface. |
| <a id="g-buffer"></a>**G-Buffer (Geometry Buffer)** | A collection of textures filled during a deferred rendering pass. Each texture stores a different per-pixel property — normals, depth, albedo, roughness, etc. — so that lighting and screen-space effects can work from this 2D snapshot instead of re-processing the 3D scene. |
| <a id="half-vector"></a>**Half Vector** | In BRDF lighting math, the vector that sits exactly halfway between the incoming light direction and the viewing direction. When the surface normal aligns with the half vector, specular highlights appear at that pixel. |
| <a id="motion-vectors"></a>**Motion Vectors** | A per-pixel buffer storing how far each pixel has moved between frames as a 2D screen-space offset. TAA uses them to reproject (warp) data from the previous frame onto the current one before blending. |
| <a id="occluders"></a>**Occluders** | Objects or pieces of geometry that block light from reaching a surface. In SSAO, nearby geometry that casts "shadow" onto a point P are that point's occluders, and their presence darkens the ambient term. |
| <a id="per-pixel-normals"></a>**Per-Pixel Normals** | The surface normal vector stored for every individual pixel in the G-buffer. Normals encode the direction a surface is "pointing" at each pixel, letting screen-space effects like SSAO know the local geometry without accessing the actual mesh. |
| <a id="point-light"></a>**Point Light** | A light source that emits light equally in all directions from a single position in 3D space, like a bare light bulb. Used as the classic example when comparing forward+ and deferred rendering. |
| <a id="projection-matrix"></a>**Projection Matrix** | A matrix that transforms 3D world/view-space coordinates into 2D screen-space coordinates. In TAA jitter sampling, a tiny sub-pixel offset is added to this matrix each frame to shift where samples land, giving more coverage over time. |
| <a id="specular-highlights"></a>**Specular Highlights** | Bright reflections of a light source visible on shiny surfaces. They appear where microfacets on the surface align with the half vector between the viewer and the light, and are controlled by the material's roughness value. |
| <a id="supersampling"></a>**Supersampling** | Collecting more than one color sample per output pixel and averaging them together to reduce aliasing. TAA achieves this over time by accumulating sub-pixel samples from multiple past frames rather than shooting extra rays within a single frame. |

---

# Sources

| # | Title | Author(s) | Publisher / Venue | Year | Link |
|---|-------|-----------|-------------------|------|------|
| 1 | Aggregate G-Buffer Anti-Aliasing | Cyril Crassin, Morgan McGuire, Kayvon Fatahalian, Aaron Lefohn | NVIDIA Research | 2015 | [Link](https://research.nvidia.com/publication/2015-02_aggregate-g-buffer-anti-aliasing) |
| 2 | TAA | Lei Yang, Shiqiu Liu, Marco Salvi | NVIDIA Corporation | 2020 | [link](http://behindthepixels.io/assets/files/TemporalAA.pdf) |
| 3 | PBR | Brent Burley | Walt Disney Animation Studios | 2012 | [link](https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf) |
| 4 | MERL 100 | Wojciech Matusik | MIT | 2007 | [Link](https://www.dropbox.com/scl/fo/ca477yl7gfibu0vg8ep99/AL1DfYSWogW2_Lokc1zFB1Y?rlkey=pot6hl4zyifdbwpnehr8ahm6p&e=1&dl=0) |
