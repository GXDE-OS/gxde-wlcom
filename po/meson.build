i18n = import('i18n')
add_project_arguments('-DGETTEXT_PACKAGE="' + meson.project_name() + '"',
  '-DLOCALEDIR="' + get_option('prefix') / get_option('localedir') + '"',
  language:'c')
i18n.gettext(meson.project_name(),
  args: ['--directory=' + source_root,
    '--add-comments=TRANSLATORS',
    '--keyword=tr'],
    preset: 'glib'
)
