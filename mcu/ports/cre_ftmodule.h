// 中文：重排与字体预览共用 TTF、OTF/CFF、可变字体及灰阶栅格器。
// English: Reflow and previews share TTF, OTF/CFF, variable fonts and the gray rasterizer.
FT_USE_MODULE(FT_Module_Class, psnames_module_class)
FT_USE_MODULE(FT_Module_Class, sfnt_module_class)
FT_USE_MODULE(FT_Driver_ClassRec, tt_driver_class)
FT_USE_MODULE(FT_Module_Class, psaux_module_class)
FT_USE_MODULE(FT_Module_Class, pshinter_module_class)
FT_USE_MODULE(FT_Driver_ClassRec, cff_driver_class)
FT_USE_MODULE(FT_Renderer_Class, ft_smooth_renderer_class)
