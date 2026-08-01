#include <glib.h>
#include <gtk/gtk.h>

static gint64 t0;

static void
log_milestone (const gchar *name)
{
  g_printerr ("[GTK3] %-10s %6.2f ms\n",
              name,
              (gdouble) (g_get_monotonic_time () - t0) / 1000.0);
}

static gboolean
quit_after_timeout (gpointer user_data)
{
  g_application_quit (G_APPLICATION (user_data));
  return G_SOURCE_REMOVE;
}

static void
on_activate (GtkApplication *app)
{
  log_milestone ("startup");

  g_autofree gchar *image_path = g_build_filename (APP_DATA_DIR, "sample.png", NULL);

  GtkWidget *window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "GTK 3 Image Viewer");
  gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);

  GtkWidget *image = gtk_image_new_from_file (image_path);
  gtk_container_add (GTK_CONTAINER (window), image);

  gtk_widget_show_all (window);
  log_milestone ("displayed");

  g_timeout_add_seconds (1, quit_after_timeout, app);
}

int
main (int argc, char **argv)
{
  t0 = g_get_monotonic_time ();

  GtkApplication *app = gtk_application_new ("dev.opencode.Gtk3ImageViewer",
                                             G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

  int status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
