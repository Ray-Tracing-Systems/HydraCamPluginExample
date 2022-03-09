# HydraCamPluginExample
Custom camera plugin examples for Hydra Renderer, example with Cmake

## Build and use 
1. Clone HydraAPI repo in some folder (for example "myfolder/HydraAPI").
2. Clone HydraCore repo (**cpu_plugin** branch for now!) in the same folder (to form "myfolder/HydraCore").
3. Build HydraCore according to it's instruction (https://github.com/Ray-Tracing-Systems/HydraCore)
4. Build this dll/so plugin with CMake
5. Edit **camera node** in scene XML to describe desired properties of your optical system:
  - attribute **cpu_plugin_dll** which is a path to your DLL
  - attribute **cpu_plugin** which is a number of implementation from your DLL ("0" means host rays plugin is disabled)
  - you can add any other attributes to camera node as you like
6. Run hydra in normal way:
```bash
hydra -inputlib "tests/demo_05" -statefile "statex_00001.xml" -out "z_out.png" -nowindow 1 
```
## Valuable parameters of cmd line for 'hydra.exe'

In general most of parameters should be set via XML for target scene. So, command line options and their combination is a bit unobvious and not always override xml parameters. So, please see several examples which may be helpful. Please ignore all other parameters, they were made for internal purposes only.

1. Normal run, took scene and render it to the image. You can also use ".hdr" format for output files.
```bash
hydra -inputlib "tests/demo_05" -statefile "statex_00001.xml" -out "z_out.png" -nowindow 1 
```

2. Debug draw of scene in a window (fly by 'WASD', run Path Tracing by pressing 'p')
```bash
hydra -inputlib "tests/demo_05" -statefile "statex_00001.xml" -cpu_fb 0 
```
Please note that "-cpu_fb" is zero which mean you can not use CPU plugin in this mode (because frame buffer is stored on GPU now which means different algorithm of contribution of samples to image), so if you press 'p' you will see black screen.
So, to see results of path tracing with pin-hole camera **set 'cpu_plugin' to 0** in camera node inside scene XML.

3. Debug run in a window (press 'p' to run Path Tracing)
```bash
hydra -inputlib "tests/demo_05" -statefile "statex_00001.xml" 
```
Please note that in this mode you will see black screen at the beggining untill you press 'p'.

4. List avaliable devices:
```bash
hydra -listdev 1
```
Than see "C:\[Hydra]\logs\devlist.txt" file. You can use parameter "-cl_device_id" further to force hydra run on a concrete device

## Valuable parameters of XML 

Here is the example of XML node for rendering settings:
```XML
<render_settings type="HydraModern" id="0">
    <width>1024</width>                          <!-- image size --> 
    <height>1024</height>                        <!-- image size -->
    <method_primary>PT</method_primary>
    <method_secondary>PT</method_secondary>
    <method_tertiary>PT</method_tertiary>
    <method_caustic>PT</method_caustic>          <!-- set "none" here to disable caustics -->
    <trace_depth>10</trace_depth>                <!-- integration depth, 1 means only emissive surfaces will be visiable, 2 is direct light, ... -->
    <diff_trace_depth>4</diff_trace_depth>       <!-- you may restrict lambert reflection seperately -->
    <maxRaysPerPixel>1024</maxRaysPerPixel>      <!-- samples per pixel. Please note that for cam. plugin this value will be multiplied with 'integrator_iters' attribute of camera -->
  </render_settings>
```

There are several essential attributes of camera for your plugin.
* cpu_plugin = "2" mean some implementation inside your DLL. "0" means plugin is disabled and will not be loaded at all.
* cpu_plugin_dll = "/home/.../libhydra_cam_plugin.so" is path to your DLL
* integrator_iters = "16" which mean hydra will trace several paths per single ray. Please use 2,4,8,16, ... to enable possible optimizations in future. 

Next, there are several essentian nodes:
* (position, look_at, up) which set transform from camera space to world space (this transform is done on GPU)
* optical_system node which set your optical system data.
* Pleas note that in current implementation you can also use 'semi_diameter' attribute instead of 'aperture_radius'

Here is the example of XML node for camera settings:
```XML
<camera id="0" name="my camera" type="uvn" integrator_iters="16" cpu_plugin="2"  cpu_plugin_dll="/home/.../libhydra_cam_plugin.so">
    <fov>30</fov>
    <nearClipPlane>0.01</nearClipPlane>
    <farClipPlane>1000</farClipPlane>
    <position>0 2 10</position>
    <look_at>0 -0.4 0</look_at>
    <up>0 1 0</up>
    <optical_system type = "tabular" name="fisheye.10mm.dat" order = "scene_to_sensor" sensor_diagonal = "0.035"> 
      <line id="0"  curvature_radius="0.0302249007"   thickness="0.00083350006" ior="1.62"        aperture_radius="0.0151700005" />
      <line id="1"  curvature_radius="0.0113931"      thickness="0.00741360011" ior="1.0"         aperture_radius="0.0103400005" />
      <line id="2"  curvature_radius="0.0752018988"   thickness="0.00106540008" ior="1.63900006"  aperture_radius="0.00889999978" />
      <line id="3"  curvature_radius="0.00833490025"  thickness="0.0111549003"  ior="1.0"         aperture_radius="0.00671000034" />
      <line id="4"  curvature_radius="0.00958819967"  thickness="0.00200540014" ior="1.65400004"  aperture_radius="0.00451000035" />
      <line id="5"  curvature_radius="0.0438676998"   thickness="0.00538950041" ior="1.0"         aperture_radius="0.00407000026" />
      <line id="6"  curvature_radius="0.0"            thickness="0.00141630007" ior="0.0"         aperture_radius="0.00275000022" />
      <line id="7"  curvature_radius="0.0294541009"   thickness="0.00219339994" ior="1.51699996"  aperture_radius="0.00298000011" />
      <line id="8"  curvature_radius="-0.00522650033" thickness="0.000971400063" ior="1.80499995" aperture_radius="0.00292000012" />
      <line id="9"  curvature_radius="-0.0142884003"  thickness="6.27000045e-05" ior="1.0"        aperture_radius="0.00298000011" />
      <line id="10" curvature_radius="-0.0223726016"  thickness="0.000940000056" ior="1.67299998" aperture_radius="0.00298000011" />
      <line id="11" curvature_radius="-0.0150404004"  thickness="0.0233591795"   ior="1.0"        aperture_radius="0.00326000014" />
    </optical_system>
  </camera>
```

In fact you can add any nodes and attributes to the 'optical_system' node or to the 'camera' node itself. Inside plugin you get the full xml as whide char string and then you can read and process any parameters you like. 

## Obtain scenes in Hydra format

1. Way number one: use HydraAPI.
   
   See examples in this repo: https://github.com/Ray-Tracing-Systems/HydraAPI-tests 
   
   Also see tests description here: http://www.raytracing.ru/tests/hydra_tests_v23b.pdf 
   
2. Way number two: install our plugin for 3ds max: https://gitlab.com/raytracingsystems 
   
   Please note that after installing plugin via installer you have to replace everything hydra in C:/[Hydra] folder with your version of hydra because instaler will override it with it's own version.
   
   Each time you press Render button in 3ds max, new state files will be saved inside "C:/[Hydra]/pluginFiles/scenelib" folder.
   
   The artist doc is located here http://www.ray-tracing.com/HydraRenderHelp.github.io/index.html 
