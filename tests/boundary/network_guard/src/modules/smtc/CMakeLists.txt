add_library(fake_mod_smtc INTERFACE)
target_link_libraries(fake_mod_smtc INTERFACE $<LINK_ONLY:Qt::Network>)
