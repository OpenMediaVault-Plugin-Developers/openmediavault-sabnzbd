<?php

/**
 * Copyright (C) 2013 OpenMediaVault Plugin Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

require_once "openmediavault/object.inc";
require_once "openmediavault/config.inc";
require_once "openmediavault/error.inc";
require_once "openmediavault/util.inc";
require_once "openmediavault/rpcservice.inc";
require_once "openmediavault/notify.inc";

class OMVRpcServiceSabnzbd extends OMVRpcServiceAbstract {

    public function getName() {
        return 'Sabnzbd';
    }

    public function initialize() {
        $this->registerMethod('getSettings');
        $this->registerMethod('setSettings');
        $this->registerMethod('doUpdateSAB');
    }

    function getSettings($params, $context) {
        $this->exec("omv-mkconf sabnzbd getsettings");
        global $xmlConfig;
        $this->validateMethodContext($context, array( "role" => OMV_ROLE_ADMINISTRATOR ));
        $object = $xmlConfig->get("//services/sabnzbd");
        if (is_null($object))
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, "//services/sabnzbd");

        // Modify result data
        $object['enable']  = boolval($object['enable']);
        $object['showtab'] = boolval($object['showtab']);
        $object['update']  = boolval($object['update']);
        return $object;
    }

    function setSettings($params, $context) {
        global $xmlConfig;
        $this->validateMethodContext($context, array( "role" => OMV_ROLE_ADMINISTRATOR ));
        $this->validateMethodParams($params, '{
            "type"       : "object",
            "properties" : {
                "enable"  : { "type" : "boolean" },
                "showtab" : { "type" : "boolean" },
                "update"  : { "type" : "boolean" }
            }
        }');

        $xpath = "//services/sabnzbd";
        $oldObject = $xmlConfig->get($xpath);

        // Prepare configuration data
        $object = array(
            "enable"  => array_boolval($params, 'enable'),
            "showtab" => array_boolval($params, 'showtab'),
            "update"  => array_boolval($params, 'update')
        );

        // Set configuration object
        if (false === $xmlConfig->replace("//services/sabnzbd", $object))
            throw new OMVException(OMVErrorMsg::E_CONFIG_SET_OBJECT_FAILED, "//services/sabnzbd");

        // Notify configuration changes
        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(OMV_NOTIFY_MODIFY, "org.openmediavault.services.sabnzbd", $object);
    }

    public function doUpdateSAB($params, $context) {
        $cmd = "omv-mkconf sabnzbd update 2>&1";
        if(0 !== $this->exec($cmd, $output))
           throw new OMVException(OMVErrorMsg::E_EXEC_FAILED, $cmd, implode("\n", $output));
        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(OMV_NOTIFY_MODIFY, "org.openmediavault.services.sabnzbd", $object);
    }
}

// Register the RPC service.
$rpcServiceMgr = &OMVRpcServiceMgr::getInstance();
$rpcServiceMgr->registerService(new OMVRpcServiceSabnzbd());

?>