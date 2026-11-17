# Links the static library: PDBHelper, ldasm and FindInCatalog are not exported from the DLL.
# msdia140.dll, which PDBHelper loads next to the executable, is copied by the BlackBone target.
add_executable(SymbolCatalogGen main.cpp)
target_link_libraries(SymbolCatalogGen PRIVATE BlackBone bcrypt version)
