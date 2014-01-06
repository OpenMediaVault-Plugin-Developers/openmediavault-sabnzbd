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

// require("js/omv/WorkspaceManager.js")
// require("js/omv/workspace/form/Panel.js")
// require("js/omv/data/Store.js")
// require("js/omv/data/Model.js")
// require("js/omv/form/plugin/LinkedFields.js")

Ext.define("OMV.module.admin.service.sabnzbd.Settings", {
    extend : "OMV.workspace.form.Panel",
    uses   : [
        "OMV.data.Model",
        "OMV.data.Store",
        "OMV.module.admin.service.sabnzbd.UpdateSAB"
    ],

    rpcService   : "Sabnzbd",
    rpcGetMethod : "getSettings",
    rpcSetMethod : "setSettings",

    plugins      : [{
        ptype        : "linkedfields",
        correlations : [{
            name       : [
                "updatesab",
            ],
            conditions : [
                { name  : "update", value : false }
            ],
            properties : "!show"
        },{
            name       : [
                "update",
            ],
            properties : "!show"
        }]
    }],

    initComponent : function () {
        var me = this;

        me.on('load', function () {
            var checked = me.findField('enable').checked;
            var showtab = me.findField('showtab').checked;
            var parent = me.up('tabpanel');

            if (!parent)
                return;

            var managementPanel = parent.down('panel[title=' + _("Web Interface") + ']');

            if (managementPanel) {
                checked ? managementPanel.enable() : managementPanel.disable();
                showtab ? managementPanel.tab.show() : managementPanel.tab.hide();
            }
        });

        me.callParent(arguments);
    },

    getFormItems : function() {
        var me = this;

        return [{
            xtype    : "fieldset",
            title    : "General settings",
            defaults : {
                labelSeparator : ""
            },
            items : [{
                xtype      : "checkbox",
                name       : "enable",
                fieldLabel : _("Enable"),
                checked    : false
            },{
                xtype      : "checkbox",
                name       : "showtab",
                fieldLabel : _("Show Tab"),
                boxLabel   : _("Show tab containing SABnzbd web interface frame."),
                checked    : false
            },{
                xtype   : "button",
                name    : "opensabnzbd",
                text    : _("SABnzbd Web Interface"),
                scope   : this,
                handler : function() {
                    var link = 'http://' + location.hostname + ':8080/';
                    window.open(link, '_blank');
                }
            },{
                xtype   : "checkbox",
                name    : "update"
            },{
                xtype   : "button",
                name    : "updatesab",
                text    : _("Update SABnzbd"),
                scope   : this,
                handler : function() {
                    var me = this;
                    OMV.MessageBox.show({
                        title   : _("Confirmation"),
                        msg     : _("Are you sure you want to update SABnzbd?"),
                        buttons : Ext.Msg.YESNO,
                        fn      : function(answer) {
                            if (answer !== "yes")
                               return;
                            // throw new OMVException(OMVErrorMsg::E_MISC_FAILURE, "You CAN NOT use this branch with this repository.");

                            OMV.Rpc.request({
                                scope   : me,
                                rpcData : {
                                    service : "Sabnzbd",
                                    method  : "doUpdateSAB",
                                    params  : {
                                        update   : 0
                                    }
                                },
                                success : function(id, success, response) {
                                    me.doReload();
                                    OMV.MessageBox.hide();
                                }
                            });
                        },
                        scope : me,
                        icon  : Ext.Msg.QUESTION
                    });
                }
            },{
                border: false,
                html: "<br />"
            }]
        }];
    }
});

OMV.WorkspaceManager.registerPanel({
    id        : "settings",
    path      : "/service/sabnzbd",
    text      : _("Settings"),
    position  : 10,
    className : "OMV.module.admin.service.sabnzbd.Settings"
});