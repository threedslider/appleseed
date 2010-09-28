
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010 Francois Beaune
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// Interface header.
#include "projectexplorer.h"

// appleseed.studio headers.
#include "mainwindow/entitybrowserwindow.h"
#include "mainwindow/entityeditorwindow.h"
#include "mainwindow/projectitem.h"
#include "utility/tweaks.h"

// appleseed.renderer headers.
#include "renderer/api/color.h"
#include "renderer/api/edf.h"
#include "renderer/api/environmentedf.h"
#include "renderer/api/environmentshader.h"
#include "renderer/api/geometry.h"
#include "renderer/api/light.h"
#include "renderer/api/material.h"
#include "renderer/api/project.h"
#include "renderer/api/scene.h"
#include "renderer/api/texture.h"

// appleseed.foundation headers.
#include "foundation/utility/containers/dictionaryarray.h"
#include "foundation/utility/foreach.h"
#include "foundation/utility/kvpair.h"
#include "foundation/utility/otherwise.h"
#include "foundation/utility/searchpaths.h"
#include "foundation/utility/string.h"
#include "foundation/utility/test.h"

// Qt headers.
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QPair>
#include <QPoint>
#include <QStringList>
#include <Qt>
#include <QTreeWidget>

// boost headers.
#include "boost/filesystem/path.hpp"

// Standard headers.
#include <cassert>
#include <memory>
#include <string>

using namespace boost;
using namespace foundation;
using namespace renderer;
using namespace std;

namespace
{
    typedef QPair<appleseed::studio::ProjectItem::Type, QVariant> ItemTypeQVariantPair;
    typedef QPair<QVariant, QVariant> QVariantPair;
}

Q_DECLARE_METATYPE(void*);
Q_DECLARE_METATYPE(appleseed::studio::ProjectItem::Type);
Q_DECLARE_METATYPE(ItemTypeQVariantPair);
Q_DECLARE_METATYPE(QVariantPair);

namespace appleseed {
namespace studio {

ProjectExplorer::ProjectExplorer(
    Project&        project,
    QTreeWidget*    tree_widget)
  : m_project(project)
  , m_project_builder(project)
  , m_tree_widget(tree_widget)
  , m_tree_widget_decorator(tree_widget)
{
    m_tree_widget->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(
        m_tree_widget, SIGNAL(customContextMenuRequested(const QPoint&)),
        this, SLOT(slot_context_menu(const QPoint&)));

    m_tree_widget_decorator.rebuild(*m_project.get_scene());
}

namespace
{
    ProjectItem::Type get_item_type(const QTreeWidgetItem* item)
    {
        assert(item);
        return static_cast<ProjectItem::Type>(item->data(0, Qt::UserRole).value<int>());
    }

    QVariant get_item_data(const QTreeWidgetItem* item)
    {
        assert(item);
        return item->data(1, Qt::UserRole);
    }

    QVariantPair get_assembly_item_data(const QTreeWidgetItem* item)
    {
        assert(item);
        return get_item_data(item).value<QVariantPair>();
    }

    template <typename T>
    T* qvariant_to_ptr(const QVariant& variant)
    {
        return static_cast<T*>(variant.value<void*>());
    }

    template <typename T>
    T& qvariant_to_ref(const QVariant& variant)
    {
        T* ptr = qvariant_to_ptr<T>(variant);
        assert(ptr);
        return *ptr;
    }
}

namespace
{
    // Get the assembly pointer stored in an *assembly* items.
    Assembly* get_assembly_from_item(const QTreeWidgetItem* item)
    {
        const QVariantPair assembly_item_data = get_assembly_item_data(item);
        return qvariant_to_ptr<Assembly>(assembly_item_data.first);
    }

    bool are_items_same_type(const QList<QTreeWidgetItem*>& items)
    {
        assert(!items.empty());

        const ProjectItem::Type first_item_type = get_item_type(items[0]);

        for (int i = 1; i < items.size(); ++i)
        {
            if (get_item_type(items[i]) != first_item_type)
                return false;
        }

        return true;
    }

    bool are_items_from_same_assembly(const QList<QTreeWidgetItem*>& items)
    {
        assert(!items.empty());

        const Assembly* first_item_assembly = get_assembly_from_item(items[0]);

        for (int i = 1; i < items.size(); ++i)
        {
            if (get_assembly_from_item(items[i]) != first_item_assembly)
                return false;
        }

        return true;
    }
}

QMenu* ProjectExplorer::build_context_menu(const QList<QTreeWidgetItem*>& items) const
{
    assert(!items.isEmpty());

    QMenu* menu = 0;

    if (items.size() == 1)
    {
        const QTreeWidgetItem* item = items.first();

        switch (get_item_type(item))
        {
          case ProjectItem::ItemAssembly:
            menu = build_assembly_context_menu();
            break;

          case ProjectItem::ItemAssemblyCollection:
            menu = build_assembly_collection_context_menu();
            break;

          case ProjectItem::ItemBSDFCollection:
            menu = build_bsdf_collection_context_menu();
            break;

          case ProjectItem::ItemMaterialCollection:
            menu = build_material_collection_context_menu();
            break;

          case ProjectItem::ItemObjectCollection:
            menu = build_object_collection_context_menu();
            break;

          case ProjectItem::ItemObjectInstance:
            menu = build_object_instance_context_menu();
            break;

          case ProjectItem::ItemSurfaceShaderCollection:
            menu = build_surface_shader_collection_context_menu();
            break;

          case ProjectItem::ItemTextureCollection:
            menu = build_texture_collection_context_menu(item);
            break;
        }
    }
    else if (are_items_same_type(items))
    {
        switch (get_item_type(items.first()))
        {
          case ProjectItem::ItemObjectInstance:
            if (are_items_from_same_assembly(items))
                menu = build_object_instance_context_menu();
            break;
        }
    }

    if (menu)
    {
        QList<QVariant> items_data;

        for (int i = 0; i < items.size(); ++i)
            items_data.push_back(get_item_data(items[i]));

        for (const_each<QList<QAction*> > i = menu->actions(); i; ++i)
            (*i)->setData(items_data);
    }

    return menu;
}

QMenu* ProjectExplorer::build_generic_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Create Assembly...", this, SLOT(slot_add_assembly()));
    return menu;
}

QMenu* ProjectExplorer::build_assembly_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Instantiate...", this, SLOT(slot_instantiate_assembly()));
    menu->addSeparator();
    menu->addAction("Import Objects...", this, SLOT(slot_import_objects_to_assembly()));
    menu->addAction("Import Textures...", this, SLOT(slot_import_textures_to_assembly()));
    menu->addSeparator();
    menu->addAction("Create BSDF...", this, SLOT(slot_add_bsdf_to_assembly()));
    menu->addAction("Create Surface Shader...", this, SLOT(slot_add_surface_shader_to_assembly()));
    menu->addAction("Create Material...", this, SLOT(slot_add_material_to_assembly()));
    return menu;
}

QMenu* ProjectExplorer::build_assembly_collection_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Create Assembly...", this, SLOT(slot_add_assembly()));
    return menu;
}

QMenu* ProjectExplorer::build_bsdf_collection_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Create BSDF...", this, SLOT(slot_add_bsdf_to_assembly()));
    return menu;
}

QMenu* ProjectExplorer::build_material_collection_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Create Material...", this, SLOT(slot_add_material_to_assembly()));
    return menu;
}

QMenu* ProjectExplorer::build_object_collection_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Import Objects...", this, SLOT(slot_import_objects_to_assembly()));
    return menu;
}

QMenu* ProjectExplorer::build_object_instance_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Assign Material...", this, SLOT(slot_assign_material_to_object_instance()));
    return menu;
}

QMenu* ProjectExplorer::build_surface_shader_collection_context_menu() const
{
    QMenu* menu = new QMenu(m_tree_widget);
    menu->addAction("Create Surface Shader...", this, SLOT(slot_add_surface_shader_to_assembly()));
    return menu;
}

QMenu* ProjectExplorer::build_texture_collection_context_menu(const QTreeWidgetItem* item) const
{
    QMenu* menu = new QMenu(m_tree_widget);

    if (qvariant_to_ptr<Assembly>(get_item_data(item)))
        menu->addAction("Import Textures...", this, SLOT(slot_import_textures_to_assembly()));
    else
        menu->addAction("Import Textures...", this, SLOT(slot_import_textures_to_scene()));

    return menu;
}

void ProjectExplorer::slot_context_menu(const QPoint& point)
{
    const QList<QTreeWidgetItem*> selected_items = m_tree_widget->selectedItems();

    QMenu* menu =
        selected_items.isEmpty()
            ? build_generic_context_menu()
            : build_context_menu(selected_items);

    if (menu)
        menu->exec(m_tree_widget->mapToGlobal(point));
}

namespace
{
    string get_entity_name(
        QWidget*            parent,
        const string&       title,
        const string&       label,
        const string&       text)
    {
        QString result;
        bool ok;

        do
        {
            result =
                QInputDialog::getText(
                    parent,
                    QString::fromStdString(title),
                    QString::fromStdString(label),
                    QLineEdit::Normal,
                    QString::fromStdString(text),
                    &ok);
        } while (ok && result.isEmpty());

        return ok ? result.toStdString() : string();
    }

    template <typename EntityContainer>
    string get_name_suggestion(
        const string&           prefix,
        const EntityContainer&  entities)
    {
        int max_number = 0;

        for (const_each<EntityContainer> i = entities; i; ++i)
        {
            const Entity& entity = *i;

            const string entity_name = entity.get_name();
            const string entity_name_prefix = entity_name.substr(0, prefix.size());

            if (entity_name_prefix == prefix)
            {
                try
                {
                    const string entity_name_suffix = entity_name.substr(prefix.size());
                    const int number = from_string<int>(entity_name_suffix);

                    if (max_number < number)
                        max_number = number;
                }
                catch (const ExceptionStringConversionError&)
                {
                }
            }
        }

        return prefix + to_string(max_number + 1);
    }

    TEST_SUITE(Studio_ProjectExplorer)
    {
        class DummyEntity
          : public Entity
        {
          public:
            explicit DummyEntity(const string& name)
              : m_name(name)
            {
            }

            virtual void release()
            {
                delete this;
            }

            virtual const char* get_name() const
            {
                return m_name.c_str();
            }

          private:
            const string m_name;
        };

        typedef TypedEntityVector<DummyEntity> DummyEntityVector;

        TEST_CASE(GetNameSuggestion_GivenZeroEntity_ReturnsNameWithFirstSuffix)
        {
            DummyEntityVector entities;

            const string result = get_name_suggestion("assembly", entities);

            EXPECT_EQ("assembly1", result);
        }

        TEST_CASE(GetNameSuggestion_GivenTwoEntitiesWithMatchingPrefixes_ReturnsNameWithNextSuffix)
        {
            DummyEntityVector entities;
            entities.insert(auto_release_ptr<DummyEntity>(new DummyEntity("assembly3")));
            entities.insert(auto_release_ptr<DummyEntity>(new DummyEntity("assembly1")));

            const string result = get_name_suggestion("assembly", entities);

            EXPECT_EQ("assembly4", result);
        }

        TEST_CASE(GetNameSuggestion_GivenEntityWithNegativeSuffix_ReturnsNameWithFirstSuffix)
        {
            DummyEntityVector entities;
            entities.insert(auto_release_ptr<DummyEntity>(new DummyEntity("assembly-5")));

            const string result = get_name_suggestion("assembly", entities);

            EXPECT_EQ("assembly1", result);
        }

        TEST_CASE(GetNameSuggestion_GivenOneEntityWithNonMatchingPrefix_ReturnsNameWithFirstSuffix)
        {
            DummyEntityVector entities;
            entities.insert(auto_release_ptr<DummyEntity>(new DummyEntity("object")));

            const string result = get_name_suggestion("assembly", entities);

            EXPECT_EQ("assembly1", result);
        }

        TEST_CASE(GetNameSuggestion_GivenOneEntityWithNonNumericSuffix_ReturnsNameWithFirstSuffix)
        {
            DummyEntityVector entities;
            entities.insert(auto_release_ptr<DummyEntity>(new DummyEntity("assembly_instance")));

            const string result = get_name_suggestion("assembly", entities);

            EXPECT_EQ("assembly1", result);
        }
    }

    QList<QVariant> get_action_data(const QObject* object)
    {
        const QAction* action = qobject_cast<const QAction*>(object);
        assert(action);

        return action->data().toList();
    }

    // Get the assembly pointer stored in the data of an *assembly* action.
    Assembly& get_assembly_from_action(const QObject* object)
    {
        const QList<QVariant> action_data = get_action_data(object);
        return qvariant_to_ref<Assembly>(action_data.first());
    }
}

void ProjectExplorer::slot_add_assembly()
{
    AssemblyContainer& assemblies = m_project.get_scene()->assemblies();

    const string assembly_name_suggestion =
        get_name_suggestion("assembly", assemblies);

    const string assembly_name =
        get_entity_name(
            m_tree_widget,
            "Create Assembly",
            "Assembly Name:",
            assembly_name_suggestion);

    if (!assembly_name.empty())
    {
        auto_release_ptr<Assembly> assembly(
            AssemblyFactory::create(
                assembly_name.c_str(),
                ParamArray()));

        m_tree_widget_decorator.insert_assembly_items(assembly.ref());

        assemblies.insert(assembly);
    }

    emit project_modified();
}

void ProjectExplorer::slot_instantiate_assembly()
{
    AssemblyInstanceContainer& assembly_instances =
        m_project.get_scene()->assembly_instances();

    const Assembly& assembly = get_assembly_from_action(sender());

    const string instance_name_suggestion =
        get_name_suggestion(
            string(assembly.get_name()) + "_inst",
            assembly_instances);

    const string instance_name =
        get_entity_name(
            m_tree_widget,
            "Instantiate Assembly",
            "Assembly Instance Name:",
            instance_name_suggestion);

    if (!instance_name.empty())
    {
        auto_release_ptr<AssemblyInstance> assembly_instance(
            AssemblyInstanceFactory::create(
                instance_name.c_str(),
                assembly,
                Transformd(Matrix4d::identity())));

        m_tree_widget_decorator.insert_scene_item(
            ProjectItem::ItemAssemblyInstance,
            assembly_instance.ref());

        assembly_instances.insert(assembly_instance);
    }

    emit project_modified();
}

void ProjectExplorer::slot_import_objects_to_assembly()
{
    QFileDialog::Options options;
    QString selected_filter;

    const QStringList filepaths =
        QFileDialog::getOpenFileNames(
            m_tree_widget,
            "Import Objects...",
            "",
            "Geometry Files (*.obj);;All Files (*.*)",
            &selected_filter,
            options);

    Assembly& assembly = get_assembly_from_action(sender());

    for (int i = 0; i < filepaths.size(); ++i)
    {
        const ProjectItemCollection project_items =
            m_project_builder.insert_objects(
                assembly,
                filepaths[i].toStdString());

        for (const_each<ProjectItemCollection> i = project_items; i; ++i)
            m_tree_widget_decorator.insert_assembly_item(assembly, *i);
    }

    emit project_modified();
}

namespace
{
    QStringList get_texture_file_paths(QWidget* parent)
    {
        QFileDialog::Options options;
        QString selected_filter;

        return
            QFileDialog::getOpenFileNames(
                parent,
                "Import Textures...",
                "",
                "Texture Files (*.exr);;All Files (*.*)",
                &selected_filter,
                options);
    }
}

void ProjectExplorer::slot_import_textures_to_assembly()
{
    const QStringList filepaths = get_texture_file_paths(m_tree_widget);

    Assembly& assembly = get_assembly_from_action(sender());

    for (int i = 0; i < filepaths.size(); ++i)
    {
        const ProjectItemCollection project_items =
            m_project_builder.insert_textures(
                assembly,
                filepaths[i].toStdString());

        for (const_each<ProjectItemCollection> i = project_items; i; ++i)
            m_tree_widget_decorator.insert_assembly_item(assembly, *i);
    }

    emit project_modified();
}

void ProjectExplorer::slot_import_textures_to_scene()
{
    const QStringList filepaths = get_texture_file_paths(m_tree_widget);

    for (int i = 0; i < filepaths.size(); ++i)
    {
        const ProjectItemCollection project_items =
            m_project_builder.insert_textures(filepaths[i].toStdString());

        for (const_each<ProjectItemCollection> i = project_items; i; ++i)
            m_tree_widget_decorator.insert_scene_item(*i);
    }

    emit project_modified();
}

namespace
{
    class ForwardEntityEditorAcceptedSignal
      : public QObject
    {
        Q_OBJECT

      public:
        explicit ForwardEntityEditorAcceptedSignal(QObject* parent, const QVariant& receiver_data)
          : QObject(parent)
          , m_receiver_data(receiver_data)
        {
        }

      public slots:
        void slot_accept(foundation::Dictionary values)
        {
            emit accepted(m_receiver_data, values);
        }

      signals:
        void accepted(QVariant receiver_data, foundation::Dictionary values);

      private:
        const QVariant m_receiver_data;
    };

    void do_open_entity_editor(
        QWidget*                                        parent,
        const string&                                   window_title,
        auto_ptr<EntityEditorWindow::IFormFactory>      form_factory,
        auto_ptr<EntityEditorWindow::IEntityBrowser>    entity_browser,
        QObject*                                        receiver,
        const QVariant&                                 receiver_data)
    {
        EntityEditorWindow* editor_window =
            new EntityEditorWindow(
                parent,
                window_title,
                form_factory,
                entity_browser);

        ForwardEntityEditorAcceptedSignal* forward_signal =
            new ForwardEntityEditorAcceptedSignal(editor_window, receiver_data);

        QObject::connect(
            editor_window, SIGNAL(accepted(foundation::Dictionary)),
            forward_signal, SLOT(slot_accept(foundation::Dictionary)));

        QObject::connect(
            forward_signal, SIGNAL(accepted(QVariant, foundation::Dictionary)),
            receiver, SLOT(slot_create_entity(QVariant, foundation::Dictionary)));

        editor_window->showNormal();
        editor_window->activateWindow();
    }

    class AssemblyEntityBrowser
      : public EntityEditorWindow::IEntityBrowser
    {
      public:
        explicit AssemblyEntityBrowser(const Assembly& assembly)
          : m_assembly(assembly)
        {
        }

        virtual StringDictionary get_entities(const string& type) const
        {
            if (type == "bsdf")
            {
                return get_entities(m_assembly.bsdfs());
            }
            else if (type == "color")
            {
                return get_entities(m_assembly.colors());
            }
            else if (type == "edf")
            {
                return get_entities(m_assembly.edfs());
            }
            else if (type == "material")
            {
                return get_entities(m_assembly.materials());
            }
            else if (type == "surface_shader")
            {
                return get_entities(m_assembly.surface_shaders());
            }
            else if (type == "texture_instance")
            {
                return get_entities(m_assembly.texture_instances());
            }
            else
            {
                return StringDictionary();
            }
        }

      private:
        const Assembly& m_assembly;

        template <typename EntityContainer>
        static StringDictionary get_entities(const EntityContainer& entities)
        {
            StringDictionary result;

            for (const_each<EntityContainer> i = entities; i; ++i)
                result.insert(i->get_name(), i->get_name());

            return result;
        }
    };

    void open_entity_editor(
        QWidget*                                        parent,
        const string&                                   window_title,
        auto_ptr<EntityEditorWindow::IFormFactory>      form_factory,
        const Assembly&                                 assembly,
        QObject*                                        receiver,
        const QVariant&                                 receiver_data)
    {
        auto_ptr<EntityEditorWindow::IEntityBrowser> entity_browser(
            new AssemblyEntityBrowser(assembly));

        do_open_entity_editor(
            parent,
            window_title,
            form_factory,
            entity_browser,
            receiver,
            receiver_data);
    }

    void open_entity_editor(
        QWidget*                                        parent,
        const string&                                   window_title,
        auto_ptr<EntityEditorWindow::IFormFactory>      form_factory,
        const Scene&                                    scene,
        QObject*                                        receiver,
        const QVariant&                                 receiver_data)
    {
        auto_ptr<EntityEditorWindow::IEntityBrowser> entity_browser;

        do_open_entity_editor(
            parent,
            window_title,
            form_factory,
            entity_browser,
            receiver,
            receiver_data);
    }
}

namespace
{
    template <typename FactoryRegistrar>
    class EntityEditorFormFactory
      : public EntityEditorWindow::IFormFactory
    {
      public:
        typedef EntityEditorWindow::WidgetDefinitionCollection WidgetDefinitionCollection;

        EntityEditorFormFactory(
            const FactoryRegistrar&     factory_registrar,
            const Assembly&             assembly,
            const string&               name_suggestion)
          : m_factory_registrar(factory_registrar)
          , m_assembly(assembly)
          , m_name_suggestion(name_suggestion)
        {
        }

        virtual void update(
            const Dictionary&           values,
            WidgetDefinitionCollection& definitions) const
        {
            definitions.clear();

            const string name = get_value(values, "name", m_name_suggestion);

            Dictionary name_widget;
            name_widget.insert("name", "name");
            name_widget.insert("label", "Name");
            name_widget.insert("widget", "text_box");
            name_widget.insert("use", "required");
            name_widget.insert("default", name);
            name_widget.insert("focus", "true");
            definitions.push_back(name_widget);

            const typename FactoryRegistrar::FactoryArrayType factories =
                m_factory_registrar.get_factories();
            Dictionary model_items;

            for (size_t i = 0; i < factories.size(); ++i)
            {
                model_items.insert(
                    factories[i]->get_human_readable_model(),
                    factories[i]->get_model());
            }

            const string model =
                get_value(
                    values,
                    "model",
                    factories.empty() ? "" : factories[0]->get_model());

            Dictionary model_widget;
            model_widget.insert("name", "model");
            model_widget.insert("label", "Model");
            model_widget.insert("widget", "dropdown_list");
            model_widget.insert("dropdown_items", model_items);
            model_widget.insert("use", "required");
            model_widget.insert("default", model);
            model_widget.insert("on_change", "rebuild_form");
            definitions.push_back(model_widget);

            if (!model.empty())
            {
                const typename FactoryRegistrar::FactoryType* factory =
                    m_factory_registrar.lookup(model.c_str());

                const DictionaryArray properties = factory->get_widget_definitions();

                for (size_t i = 0; i < properties.size(); ++i)
                    definitions.push_back(properties[i]);
            }
        }

      private:
        const FactoryRegistrar&     m_factory_registrar;
        const Assembly&             m_assembly;
        const string                m_name_suggestion;
    };
}

void ProjectExplorer::slot_add_bsdf_to_assembly()
{
    const Assembly& assembly = get_assembly_from_action(sender());

    auto_ptr<EntityEditorWindow::IFormFactory> form_factory(
        new EntityEditorFormFactory<BSDFFactoryRegistrar>(
            m_bsdf_factory_registrar,
            assembly,
            get_name_suggestion("bsdf", assembly.bsdfs())));

    const QVariant receiver_data(
        QVariant::fromValue(
            ItemTypeQVariantPair(
                ProjectItem::ItemBSDF,
                get_action_data(sender()))));

    open_entity_editor(
        m_tree_widget,
        "Create BSDF",
        form_factory,
        assembly,
        this,
        receiver_data);
}

void ProjectExplorer::slot_add_surface_shader_to_assembly()
{
    const Assembly& assembly = get_assembly_from_action(sender());

    auto_ptr<EntityEditorWindow::IFormFactory> form_factory(
        new EntityEditorFormFactory<SurfaceShaderFactoryRegistrar>(
            m_surface_shader_factory_registrar,
            assembly,
            get_name_suggestion("surface_shader", assembly.surface_shaders())));

    const QVariant receiver_data(
        QVariant::fromValue(
            ItemTypeQVariantPair(
                ProjectItem::ItemSurfaceShader,
                get_action_data(sender()))));

    open_entity_editor(
        m_tree_widget,
        "Create Surface Shader",
        form_factory,
        assembly,
        this,
        receiver_data);
}

namespace
{
    class MaterialEditorFormFactory
      : public EntityEditorWindow::IFormFactory
    {
      public:
        typedef EntityEditorWindow::WidgetDefinitionCollection WidgetDefinitionCollection;

        explicit MaterialEditorFormFactory(const Assembly& assembly)
          : m_assembly(assembly)
        {
        }

        virtual void update(
            const Dictionary&           values,
            WidgetDefinitionCollection& definitions) const
        {
            definitions.clear();

            const string material_name_suggestion =
                get_name_suggestion("material", m_assembly.materials());

            {
                Dictionary widget;
                widget.insert("name", "name");
                widget.insert("label", "Name");
                widget.insert("widget", "text_box");
                widget.insert("use", "required");
                widget.insert("default", material_name_suggestion);
                widget.insert("focus", "true");
                definitions.push_back(widget);
            }

            {
                Dictionary entity_types;
                entity_types.insert("bsdf", "BSDF");

                Dictionary widget;
                widget.insert("name", "bsdf");
                widget.insert("label", "BSDF");
                widget.insert("widget", "entity_picker");
                widget.insert("entity_types", entity_types);
                widget.insert("use", "optional");
                definitions.push_back(widget);
            }

            {
                Dictionary entity_types;
                entity_types.insert("edf", "EDF");

                Dictionary widget;
                widget.insert("name", "edf");
                widget.insert("label", "EDF");
                widget.insert("widget", "entity_picker");
                widget.insert("entity_types", entity_types);
                widget.insert("use", "optional");
                definitions.push_back(widget);
            }

            {
                Dictionary entity_types;
                entity_types.insert("surface_shader", "Surface Shaders");

                Dictionary widget;
                widget.insert("name", "surface_shader");
                widget.insert("label", "Surface Shader");
                widget.insert("widget", "entity_picker");
                widget.insert("entity_types", entity_types);
                widget.insert("use", "required");
                definitions.push_back(widget);
            }
        }

      private:
        const Assembly& m_assembly;
    };
}

void ProjectExplorer::slot_add_material_to_assembly()
{
    const Assembly& assembly = get_assembly_from_action(sender());

    auto_ptr<EntityEditorWindow::IFormFactory> form_factory(
        new MaterialEditorFormFactory(assembly));

    const QVariant receiver_data(
        QVariant::fromValue(
            ItemTypeQVariantPair(
                ProjectItem::ItemMaterial,
                get_action_data(sender()))));

    open_entity_editor(
        m_tree_widget,
        "Create Material",
        form_factory,
        assembly,
        this,
        receiver_data);
}

namespace
{
    const KeyValuePair<ProjectItem::Type, const char*> EntityNames[] =
    {
        { ProjectItem::ItemBSDF, "BSDF" },
        { ProjectItem::ItemMaterial, "material" }
    };

    void display_entity_creation_error(
        const ProjectItem::Type item_type,
        const QString&          message)
    {
        QMessageBox msgbox;
        msgbox.setWindowTitle(
            QString("Failed to create %1").arg(
                FOUNDATION_LOOKUP_KVPAIR_ARRAY(EntityNames, item_type)->m_value));
        msgbox.setIcon(QMessageBox::Warning);
        msgbox.setText(message);
        msgbox.setStandardButtons(QMessageBox::Ok);
        msgbox.setDefaultButton(QMessageBox::Ok);
        set_minimum_width(msgbox, 300);
        msgbox.exec();
    }
}

void ProjectExplorer::slot_create_entity(QVariant payload, Dictionary values)
{
    const ItemTypeQVariantPair item = payload.value<ItemTypeQVariantPair>();
    const ProjectItem::Type item_type = item.first;
    Assembly& assembly = qvariant_to_ref<Assembly>(item.second.toList().first());

    try
    {
        switch (item_type)
        {
          case ProjectItem::ItemBSDF:
            m_tree_widget_decorator.insert_assembly_item(
                assembly,
                m_project_builder.insert_bsdf(assembly, values));
            break;

          case ProjectItem::ItemMaterial:
            m_tree_widget_decorator.insert_assembly_item(
                assembly,
                m_project_builder.insert_material(assembly, values));
            break;

          case ProjectItem::ItemSurfaceShader:
            m_tree_widget_decorator.insert_assembly_item(
                assembly,
                m_project_builder.insert_surface_shader(assembly, values));
            break;

          assert_otherwise;
        }

        // Close the entity editor.
        qobject_cast<QWidget*>(sender()->parent())->close();

        emit project_modified();
    }
    catch (const ExceptionDictionaryItemNotFound& e)
    {
        display_entity_creation_error(
            item_type,
            QString("Required parameter \"%0\" missing.").arg(e.string()));
    }
    catch (const ExceptionUnknownEntity& e)
    {
        display_entity_creation_error(
            item_type,
            QString("Unknown entity \"%0\".").arg(e.string()));
    }
}

namespace
{
    class ForwardEntityBrowserAcceptedSignal
      : public QObject
    {
        Q_OBJECT

      public:
        ForwardEntityBrowserAcceptedSignal(QObject* parent, const QList<QVariant>& items_data)
          : QObject(parent)
          , m_items_data(items_data)
        {
        }

      public slots:
        void slot_accept(QString page_name, QString entity_name)
        {
            emit accepted(m_items_data, page_name, entity_name);
        }

      signals:
        void accepted(QList<QVariant> items_data, QString page_name, QString entity_name);

      private:
        const QList<QVariant> m_items_data;
    };
}

void ProjectExplorer::slot_assign_material_to_object_instance()
{
    const QList<QVariant> items_data = get_action_data(sender());

    const QVariantPair first_item = items_data.first().value<QVariantPair>();
    const Assembly& assembly = qvariant_to_ref<Assembly>(first_item.first);
    const ObjectInstance& first_object_instance =
        qvariant_to_ref<ObjectInstance>(first_item.second);

    const QString window_title =
        items_data.size() == 1
            ? QString("Assign Material to %1").arg(first_object_instance.get_name())
            : QString("Assign Material to Multiple Object Instances");

    EntityBrowserWindow* browser_window =
        new EntityBrowserWindow(
            m_tree_widget,
            window_title.toStdString());

    AssemblyEntityBrowser entity_browser(assembly);
    browser_window->add_items_page(
        "material",
        "Materials",
        entity_browser.get_entities(string("material")));

    ForwardEntityBrowserAcceptedSignal* forward_signal =
        new ForwardEntityBrowserAcceptedSignal(
            browser_window,
            items_data);

    QObject::connect(
        browser_window, SIGNAL(accepted(QString, QString)),
        forward_signal, SLOT(slot_accept(QString, QString)));

    QObject::connect(
        forward_signal, SIGNAL(accepted(QList<QVariant>, QString, QString)),
        this, SLOT(slot_do_assign_material_to_object_instance(QList<QVariant>, QString, QString)));

    browser_window->showNormal();
    browser_window->activateWindow();
}

void ProjectExplorer::slot_do_assign_material_to_object_instance(
    QList<QVariant> items_data,
    QString         page_name,
    QString         entity_name)
{
    // Retrieve the index within the assembly of the selected material.
    const QVariantPair first_item = items_data.first().value<QVariantPair>();
    const Assembly& assembly = qvariant_to_ref<Assembly>(first_item.first);
    const size_t material_index = assembly.materials().get_index(entity_name.toAscii());
    assert(material_index != ~size_t(0));

    // Assign the material to all selected object instances.
    for (int i = 0; i < items_data.size(); ++i)
    {
        const QVariantPair item = items_data[i].value<QVariantPair>();
        ObjectInstance& object_instance =
            qvariant_to_ref<ObjectInstance>(item.second);
        object_instance.set_material_index(0, material_index);
    }

    // Close the entity browser.
    qobject_cast<QWidget*>(sender()->parent())->close();

    emit project_modified();
}

}   // namespace studio
}   // namespace appleseed

#include "mainwindow/moc_cpp_projectexplorer.cxx"
