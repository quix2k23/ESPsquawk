#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

void web_config_init(void);

/* Stops the HTTP server (used when webserver_en is toggled off at
 * runtime). Safe to call even if the server isn't running. */
void web_config_stop(void);

#endif
