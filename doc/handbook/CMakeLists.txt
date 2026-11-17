# Panero installs the (upstream Krusader) handbook under its own name so it does not
# collide with an installed Krusader package.
kdoctools_create_handbook(index.docbook INSTALL_DESTINATION ${KDE_INSTALL_DOCBUNDLEDIR}/en SUBDIR panero)
kdoctools_create_manpage(man-panero.1.docbook 1 INSTALL_DESTINATION ${KDE_INSTALL_MANDIR})
