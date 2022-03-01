# HydraCamPluginExample
Custom camera plugin examples for Hydra Renderer, example with Cmake

# Build and use 
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

